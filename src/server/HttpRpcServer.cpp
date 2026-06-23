#include "instrument-script-server/server/HttpRpcServer.hpp"
#include "instrument-script-server/server/CommandHandlers.hpp"
#include "instserver/server/v1/daemon_messages.pb.h"
#include <instrument-log/inst_logging.h>

#ifdef _WIN32
#include "instrument-script-server/compat/WinSock.hpp"
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

namespace instserver::server {

namespace {
constexpr int BACKLOG = 8;
constexpr size_t MAX_HEADER_READ = 64 * 1024; // 64 KB

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
bool read_http_headers(int fd, std::string &out_headers,
                       std::string &extra_data) {
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
  resp << "Content-Type: application/x-protobuf\r\n";
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
    LOG_ERROR("RPC", "SOCKET", "Failed to create socket: %d",
              WSAGetLastError());
    running_ = false;
    return;
  }
#else
  listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
  if (listen_fd_ < 0) {
    LOG_ERROR("RPC", "SOCKET", "Failed to create socket: %s", strerror(errno));
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

  struct sockaddr_in addr{};
  std::memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);

#ifdef _WIN32
  if (InetPton(AF_INET, "127.0.0.1", &addr.sin_addr) != 1) {
    LOG_ERROR("RPC", "ADDR", "InetPton failed");
    // handle error
    running_ = false;
    return;
  }
#else
  if (inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr) != 1) {
    LOG_ERROR("RPC", "ADDR", "inet_pton failed: %s", strerror(errno));
    // handle error
    running_ = false;
    return;
  }
#endif

  if (bind(listen_fd_, reinterpret_cast<struct sockaddr *>(&addr),
           sizeof(addr)) < 0) {
#ifdef _WIN32
    LOG_ERROR("RPC", "BIND", "bind failed: %s", WSAGetLastError());
    closesocket(listen_fd_);
#else
    LOG_ERROR("RPC", "BIND", "bind failed: %s", strerror(errno));
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
    struct sockaddr_in sin{};
    socklen_t len = sizeof(sin);
    if (getsockname(listen_fd_, reinterpret_cast<struct sockaddr *>(&sin),
                    &len) == 0) {
      bound_port_ = ntohs(sin.sin_port);
    }
  } else {
    bound_port_ = port;
  }

  LOG_INFO("RPC", "START", "HTTP RPC server listening on 127.0.0.1:%d",
           bound_port_);

  // Accept loop
  while (running_) {
    struct sockaddr_in client_addr{};
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
      LOG_WARN("RPC", "ACCEPT", "accept failed: %d", err);
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      continue;
    }
    int client_socket = static_cast<int>(client_fd);
#else
    int client_socket =
        accept(listen_fd_, reinterpret_cast<struct sockaddr *>(&client_addr),
               &client_len);
    if (client_socket < 0) {
      if (!running_) {
        break;
      }
      LOG_WARN("RPC", "ACCEPT", "accept failed: %s", strerror(errno));
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
    if (!request_line.empty() && request_line.back() == '\r') {
      request_line.pop_back();
    }

    std::string method;
    std::string path;
    std::string proto;
    {
      std::istringstream rl(request_line);
      rl >> method >> path >> proto;
    }

    LOG_DEBUG("RPC", "REQUEST", "Method: %s, Path: %s", method.c_str(),
              path.c_str());

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
      v1::RpcResponse resp;
      auto *err = resp.mutable_error();
      err->mutable_standard_response()->set_ok(false);
      err->mutable_standard_response()->set_message(
          "Only POST /rpc is supported");

      std::string out;
      resp.SerializeToString(&out);
      send_http_response(client_socket, 404, out);
      close_socket(client_socket);
      continue;
    }

    try {
      v1::RpcRequest req;

      if (!req.ParseFromString(body)) {
        v1::RpcResponse resp;

        auto *err = resp.mutable_error();
        err->mutable_standard_response()->set_ok(false);
        err->mutable_standard_response()->set_message(
            "failed to parse request");

        std::string out;
        resp.SerializeToString(&out);

        send_http_response(client_socket, 400, out);
        close_socket(client_socket);
        continue;
      }

      v1::RpcResponse resp;
      int rc = 0;

      switch (req.request_case()) {

      case v1::RpcRequest::kDaemonStatus:
        rc = handle_daemon_status(req.daemon_status(),
                                  resp.mutable_daemon_status());
        break;

      case v1::RpcRequest::kStartInstrument:
        rc = handle_start_instrument(req.start_instrument(),
                                     resp.mutable_start_instrument());
        break;

      case v1::RpcRequest::kStopInstrument:
        rc = handle_stop_instrument(req.stop_instrument(),
                                    resp.mutable_stop_instrument());
        break;

      case v1::RpcRequest::kInstrumentStatus:
        rc = handle_instrument_status(req.instrument_status(),
                                      resp.mutable_instrument_status());
        break;

      case v1::RpcRequest::kListInstrument:
        rc = handle_list_instruments(req.list_instrument(),
                                     resp.mutable_list_instrument());
        break;

      case v1::RpcRequest::kMeasureJob:
        rc = handle_measure_job(req.measure_job(), resp.mutable_measure_job());
        break;

      case v1::RpcRequest::kJobStatus:
        rc = handle_job_status(req.job_status(), resp.mutable_job_status());
        break;

      case v1::RpcRequest::kMeasureJobResult:
        rc = handle_measure_job_result(req.measure_job_result(),
                                       resp.mutable_measure_job_result());
        break;

      case v1::RpcRequest::kJobList:
        rc = handle_job_list(req.job_list(), resp.mutable_job_list());
        break;

      case v1::RpcRequest::kCancelJob:
        rc = handle_cancel_job(req.cancel_job(), resp.mutable_cancel_job());
        break;

      case v1::RpcRequest::kDiscover:
        rc = handle_discover(req.discover(), resp.mutable_discover());
        break;

      case v1::RpcRequest::kListDataBuffers:
        rc = handle_list_buffers(req.list_data_buffers(),
                                 resp.mutable_list_data_buffers());
        break;

      case v1::RpcRequest::kReleaseBuffer:
        rc = handle_release_buffer(req.release_buffer(),
                                   resp.mutable_release_buffer());
        break;

      case v1::RpcRequest::kGetBufferMetadata:
        rc = handle_get_buffer_metadata(req.get_buffer_metadata(),
                                        resp.mutable_get_buffer_metadata());
        break;

      case v1::RpcRequest::kDaemonStop:
        rc = handle_daemon_stop(req.daemon_stop(), nullptr);
        break;

      case v1::RpcRequest::REQUEST_NOT_SET:
      default:
        rc = 1;
        {
          auto *err = resp.mutable_error();
          err->mutable_standard_response()->set_ok(false);
          err->mutable_standard_response()->set_message("unknown request type");
        }
        break;
      }
      std::string out;
      resp.SerializeToString(&out);

      int http_status = (rc == 0) ? 200 : 500;
      send_http_response(client_socket, http_status, out);

    } catch (const std::exception &e) {

      v1::RpcResponse resp;

      auto *err = resp.mutable_error();
      err->mutable_standard_response()->set_ok(false);
      err->mutable_standard_response()->set_message(std::string("exception: ") +
                                                    e.what());

      std::string out;
      resp.SerializeToString(&out);

      send_http_response(client_socket, 500, out);
    }

    close_socket(client_socket);
  } // accept loop

  close_socket(listen_fd_);
  LOG_INFO("RPC", "STOP", "HTTP RPC server stopped");
  running_ = false;
}

} // namespace instserver::server
