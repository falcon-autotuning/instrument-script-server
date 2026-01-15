#include "instrument-server/server/HttpRpcServer.hpp"
#include "instrument-server/Logger.hpp"
#include "instrument-server/server/CommandHandlers.hpp"
#include <nlohmann/json.hpp>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "Ws2_32.lib")
using socklen_t = int;
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#endif

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>

using json = nlohmann::json;

namespace instserver {
namespace server {

namespace {
constexpr int BACKLOG = 8;
constexpr size_t MAX_HEADER_READ = 64 * 1024; // 64 KB
constexpr int DEFAULT_PORT = 8555;

// Cross-platform close
inline void close_socket(int fd) {
#ifdef _WIN32
  closesocket(fd);
#else
  close(fd);
#endif
}

// Read exactly n bytes into buffer. Returns true if successful.
bool read_n(int fd, char *buf, size_t n) {
  size_t read_total = 0;
  while (read_total < n) {
    int r = static_cast<int>(
        recv(fd, buf + read_total, static_cast<int>(n - read_total), 0));
    if (r <= 0)
      return false;
    read_total += static_cast<size_t>(r);
  }
  return true;
}

// Read until "\r\n\r\n" or until limit. Returns true and fills out header
// string. Also returns any extra bytes read after the headers in extra_data.
bool read_http_headers(int fd, std::string &out_headers, std::string &extra_data) {
  out_headers.clear();
  extra_data.clear();
  char buf[1024];
  size_t total = 0;
  while (total < MAX_HEADER_READ) {
    int r = static_cast<int>(recv(fd, buf, sizeof(buf), 0));
    if (r <= 0)
      return false;
    out_headers.append(buf, buf + r);
    total += static_cast<size_t>(r);
    auto pos = out_headers.find("\r\n\r\n");
    if (pos != std::string::npos) {
      // Found end of headers. Extract any extra data after headers.
      size_t headers_end = pos + 4; // After "\r\n\r\n"
      if (headers_end < out_headers.size()) {
        extra_data = out_headers.substr(headers_end);
        out_headers = out_headers.substr(0, headers_end);
      }
      return true;
    }
  }
  return false;
}

// Parse Content-Length from headers (case-insensitive). Returns -1 if not
// found.
int parse_content_length(const std::string &headers) {
  std::istringstream ss(headers);
  std::string line;
  while (std::getline(ss, line)) {
    // Trim
    if (!line.empty() && line.back() == '\r')
      line.pop_back();
    std::string lower = line;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    auto pos = lower.find("content-length:");
    if (pos != std::string::npos) {
      auto val = line.substr(pos + strlen("content-length:"));
      // trim
      size_t i = 0;
      while (i < val.size() && isspace((unsigned char)val[i]))
        ++i;
      try {
        return std::stoi(val.substr(i));
      } catch (...) {
        return -1;
      }
    }
  }
  return -1;
}

// Very small HTTP reply helper
void send_http_response(int fd, int status_code, const std::string &body) {
  std::ostringstream resp;
  resp << "HTTP/1.0 " << status_code << " \r\n";
  resp << "Content-Type: application/json\r\n";
  resp << "Content-Length: " << body.size() << "\r\n";
  resp << "Connection: close\r\n";
  resp << "\r\n";
  resp << body;
  std::string s = resp.str();
  size_t sent = 0;
  while (sent < s.size()) {
    int w = static_cast<int>(
        send(fd, s.data() + sent, static_cast<int>(s.size() - sent), 0));
    if (w <= 0)
      break;
    sent += static_cast<size_t>(w);
  }
}

} // namespace

HttpRpcServer::HttpRpcServer() : running_(false), bound_port_(0) {}

HttpRpcServer::~HttpRpcServer() { stop(); }

bool HttpRpcServer::start(uint16_t port) {
  if (running_.exchange(true)) {
    // already running
    return true;
  }

#ifdef _WIN32
  WSADATA wsa_data;
  if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
    LOG_ERROR("RPC", "WSASTART", "WSAStartup failed");
    running_ = false;
    return false;
  }
#endif

  server_thread_ = std::thread(&HttpRpcServer::run_loop, this, port);
  // Wait briefly for server to bind (small sleep)
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  return true;
}

void HttpRpcServer::stop() {
  if (!running_.exchange(false)) {
    return;
  }

  // Close the listening socket to unblock accept() call
  // Use shutdown first to interrupt any blocking operations
  if (listen_fd_ >= 0) {
#ifdef _WIN32
    shutdown(listen_fd_, SD_BOTH);
    closesocket(listen_fd_);
#else
    shutdown(listen_fd_, SHUT_RDWR);
    close(listen_fd_);
#endif
    listen_fd_ = -1;
  }

  // If server thread is joinable, join it.
  if (server_thread_.joinable()) {
    server_thread_.join();
  }

#ifdef _WIN32
  WSACleanup();
#endif
}

uint16_t HttpRpcServer::port() const { return bound_port_; }

void HttpRpcServer::run_loop(uint16_t port) {
  listen_fd_ = -1;
  bound_port_ = 0;

#ifdef _WIN32
  listen_fd_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (listen_fd_ == INVALID_SOCKET) {
    LOG_ERROR("RPC", "SOCKET", "Failed to create socket: {}",
              WSAGetLastError());
    running_ = false;
    return;
  }
#else
  listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
  if (listen_fd_ < 0) {
    LOG_ERROR("RPC", "SOCKET", "Failed to create socket: {}", strerror(errno));
    running_ = false;
    return;
  }
#endif

  // Allow immediate reuse
  int opt = 1;
#ifdef _WIN32
  setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, (const char *)&opt,
             sizeof(opt));
#else
  setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#endif

  struct sockaddr_in addr;
  std::memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = inet_addr("127.0.0.1");
  addr.sin_port = htons(port);

  if (bind(listen_fd_, reinterpret_cast<struct sockaddr *>(&addr),
           sizeof(addr)) < 0) {
#ifdef _WIN32
    LOG_ERROR("RPC", "BIND", "bind failed: {}", WSAGetLastError());
    closesocket(listen_fd_);
#else
    LOG_ERROR("RPC", "BIND", "bind failed: {}", strerror(errno));
    close(listen_fd_);
#endif
    listen_fd_ = -1;
    running_ = false;
    return;
  }

  if (listen(listen_fd_, BACKLOG) < 0) {
    LOG_ERROR("RPC", "LISTEN", "listen failed");
#ifdef _WIN32
    closesocket(listen_fd_);
#else
    close(listen_fd_);
#endif
    listen_fd_ = -1;
    running_ = false;
    return;
  }

  // Set bound_port_ AFTER listen succeeds so daemon knows server is ready
  // If port was 0, query assigned port
  if (port == 0) {
    struct sockaddr_in sin;
    socklen_t len = sizeof(sin);
    if (getsockname(listen_fd_, reinterpret_cast<struct sockaddr *>(&sin),
                    &len) == 0) {
      bound_port_ = ntohs(sin.sin_port);
    }
  } else {
    bound_port_ = port;
  }

  LOG_INFO("RPC", "START", "HTTP RPC server listening on 127.0.0.1:{}",
           bound_port_);

  // Accept loop
  while (running_) {
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
#ifdef _WIN32
    SOCKET client_fd =
        accept(listen_fd_, reinterpret_cast<struct sockaddr *>(&client_addr),
               &client_len);
    if (client_fd == INVALID_SOCKET) {
      // If we were asked to stop, break
      int err = WSAGetLastError();
      if (!running_)
        break;
      LOG_WARN("RPC", "ACCEPT", "accept failed: {}", err);
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      continue;
    }
    int client_socket = static_cast<int>(client_fd);
#else
    int client_socket =
        accept(listen_fd_, reinterpret_cast<struct sockaddr *>(&client_addr),
               &client_len);
    if (client_socket < 0) {
      if (!running_)
        break;
      LOG_WARN("RPC", "ACCEPT", "accept failed: {}", strerror(errno));
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      continue;
    }
#endif

    // Handle client in a short-lived handler (single-threaded for simplicity)
    std::string headers;
    std::string extra_data;
    if (!read_http_headers(client_socket, headers, extra_data)) {
      LOG_WARN("RPC", "REQUEST", "Failed to read HTTP headers");
      close_socket(client_socket);
      continue;
    }

    LOG_DEBUG("RPC", "REQUEST", "Received request headers");

    // Determine request line (first line)
    std::istringstream hs(headers);
    std::string request_line;
    std::getline(hs, request_line);
    if (!request_line.empty() && request_line.back() == '\r')
      request_line.pop_back();

    std::string method, path, proto;
    {
      std::istringstream rl(request_line);
      rl >> method >> path >> proto;
    }

    LOG_DEBUG("RPC", "REQUEST", "Method: {}, Path: {}", method, path);

    int content_len = parse_content_length(headers);

    std::string body;
    if (content_len > 0) {
      // Start with any extra data read after headers
      body = extra_data;
      
      // Read remaining body if needed
      size_t remaining = static_cast<size_t>(content_len) - body.size();
      if (remaining > 0) {
        size_t old_size = body.size();
        body.resize(static_cast<size_t>(content_len));
        if (!read_n(client_socket, &body[old_size], remaining)) {
          LOG_WARN("RPC", "REQUEST", "Failed to read request body");
          close_socket(client_socket);
          continue;
        }
      }
    } else {
      body.clear();
    }

    // We only support POST /rpc
    if (!(method == "POST" && (path == "/rpc" || path == "/rpc/"))) {
      json resp_json;
      resp_json["ok"] = false;
      resp_json["error"] = "Only POST /rpc is supported";
      send_http_response(client_socket, 404, resp_json.dump());
      close_socket(client_socket);
      continue;
    }

    try {
      auto req = json::parse(body);
      std::string command = req.value("command", "");
      json params = req.value("params", json::object());

      json resp;
      resp["ok"] = false;

      // Dispatch to full command handlers (including job endpoints)
      int rc = 1;
      if (command == "list") {
        rc = server::handle_list(params, resp);
      } else if (command == "status") {
        rc = server::handle_status(params, resp);
      } else if (command == "start") {
        rc = server::handle_start(params, resp);
      } else if (command == "stop") {
        rc = server::handle_stop(params, resp);
      } else if (command == "daemon") {
        rc = server::handle_daemon(params, resp);
      } else if (command == "measure") {
        rc = server::handle_measure(params, resp);
      } else if (command == "test") {
        rc = server::handle_test(params, resp);
      } else if (command == "discover") {
        rc = server::handle_discover(params, resp);
      } else if (command == "plugins") {
        rc = server::handle_plugins(params, resp);
      } else if (command == "submit_job") {
        rc = server::handle_submit_job(params, resp);
      } else if (command == "submit_measure") {
        rc = server::handle_submit_measure(params, resp);
      } else if (command == "job_status") {
        rc = server::handle_job_status(params, resp);
      } else if (command == "job_result") {
        rc = server::handle_job_result(params, resp);
      } else if (command == "job_list") {
        rc = server::handle_job_list(params, resp);
      } else if (command == "job_cancel") {
        rc = server::handle_job_cancel(params, resp);
      } else {
        resp["ok"] = false;
        resp["error"] = "unknown command";
        rc = 1;
      }

      // Translate rc to HTTP status
      int http_status = (rc == 0) ? 200 : 500;
      send_http_response(client_socket, http_status, resp.dump());
    } catch (const std::exception &e) {
      json err;
      err["ok"] = false;
      err["error"] = std::string("exception: ") + e.what();
      send_http_response(client_socket, 500, err.dump());
    }

    close_socket(client_socket);
  } // accept loop

  close_socket(listen_fd_);
  LOG_INFO("RPC", "STOP", "HTTP RPC server stopped");
  running_ = false;
}

} // namespace server
} // namespace instserver
