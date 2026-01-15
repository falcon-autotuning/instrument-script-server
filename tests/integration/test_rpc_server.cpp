#include "instrument-server/server/ServerDaemon.hpp"
#include <gtest/gtest.h>

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
#include <unistd.h>
#endif

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>

using json = nlohmann::json;

static bool send_http_post(const std::string &host, int port,
                           const std::string &path, const std::string &body,
                           std::string &out_response_body,
                           int timeout_ms = 2000) {
#ifdef _WIN32
  WSADATA wsa_data;
  if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
    return false;
  }
#endif

  int sockfd = -1;
  bool connected = false;
  const int max_attempts = 20;
  int attempts = 0;
  while (!connected && attempts < max_attempts) {
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
#ifdef _WIN32
      WSACleanup();
#endif
      return false;
    }

    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(static_cast<uint16_t>(port));
    serv_addr.sin_addr.s_addr = inet_addr(host.c_str());

    // connect
    if (connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
#ifdef _WIN32
      closesocket(sockfd);
#else
      close(sockfd);
#endif
      // sleep and retry
      ++attempts;
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
      continue;
    }
    connected = true;
    break;
  }

#ifdef _WIN32
  if (!connected)
    WSACleanup();
#else
  if (!connected)
    ;
#endif

  if (!connected)
    return false;

  std::ostringstream req;
  req << "POST " << path << " HTTP/1.0\r\n";
  req << "Host: " << host << "\r\n";
  req << "Content-Type: application/json\r\n";
  req << "Content-Length: " << body.size() << "\r\n";
  req << "Connection: close\r\n";
  req << "\r\n";
  req << body;

  std::string reqs = req.str();
  size_t sent = 0;
  while (sent < reqs.size()) {
    int w = static_cast<int>(send(sockfd, reqs.data() + sent,
                                  static_cast<int>(reqs.size() - sent), 0));
    if (w <= 0)
      break;
    sent += static_cast<size_t>(w);
  }

  // read response
  std::string response;
  char buf[1024];
  int r;
  while ((r = static_cast<int>(recv(sockfd, buf, sizeof(buf), 0))) > 0) {
    response.append(buf, buf + r);
  }

#ifdef _WIN32
  closesocket(sockfd);
  WSACleanup();
#else
  close(sockfd);
#endif

  auto pos = response.find("\r\n\r\n");
  if (pos == std::string::npos) {
    out_response_body.clear();
    return false;
  }
  out_response_body = response.substr(pos + 4);
  return true;
}

class RpcServerTest : public ::testing::Test {
protected:
  void SetUp() override {
    // RPC address tests expect loopback:8555 by default (keeps test stable).
    rpc_host_ = "127.0.0.1";
    rpc_port_ = 8555;

    // Start daemon directly in-process instead of via CLI
    auto &daemon = instserver::ServerDaemon::instance();
    if (!daemon.is_running()) {
      daemon.set_rpc_port(rpc_port_);
      if (!daemon.start()) {
        GTEST_SKIP() << "Failed to start daemon";
        return;
      }
      started_daemon_ = true;
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    } else {
      started_daemon_ = false;
    }

    // Poll the RPC /rpc endpoint until we receive a parseable JSON response
    // or time out. This avoids races where the test sends requests before the
    // HTTP server is accepting connections.
    const int timeout_ms = 5000;
    const int poll_interval_ms = 100;
    int waited = 0;
    bool ready = false;
    while (waited < timeout_ms) {
      std::string body = R"({"command":"list","params":{}})";
      std::string resp;
      bool connected =
          send_http_post(rpc_host_, rpc_port_, "/rpc", body, resp, 500);
      if (connected && !resp.empty()) {
        // If we get any parseable JSON back, assume the server is up. Do not
        // assert on "ok" here because the server may be up but not fully
        // initialized for other reasons; the test case will assert handler
        // semantics itself.
        try {
          auto j = nlohmann::json::parse(resp);
          ready = true;
          break;
        } catch (...) {
          // Not JSON; continue polling
        }
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(poll_interval_ms));
      waited += poll_interval_ms;
    }

    if (!ready) {
      // If we started the daemon but the RPC endpoint didn't come up, try to
      // stop the daemon we started to avoid leaving processes running.
      if (started_daemon_) {
        daemon.stop();
      }
      FAIL()
          << "RPC server not responding on " << rpc_host_ << ":" << rpc_port_
          << " after " << timeout_ms
          << "ms. Ensure RPC server can start and listen on that port.";
    }
  }

  void TearDown() override {
    // If we started the daemon ourselves, stop it now.
    if (started_daemon_) {
      auto &daemon = instserver::ServerDaemon::instance();
      daemon.stop();
    }
  }

  std::string rpc_host_;
  int rpc_port_{0};
  bool started_daemon_{false};
};

TEST_F(RpcServerTest, ListReturnsOk) {
  std::string body = R"({"command":"list","params":{}})";
  std::string resp;
  bool ok = send_http_post("127.0.0.1", 8555, "/rpc", body, resp);
  ASSERT_TRUE(ok);
  ASSERT_FALSE(resp.empty());

  json j = json::parse(resp);
  ASSERT_TRUE(j.contains("ok"));
  ASSERT_TRUE(j["ok"].get<bool>());
  ASSERT_TRUE(j.contains("instruments"));
  ASSERT_TRUE(j["instruments"].is_array());
}

TEST_F(RpcServerTest, PluginsReturnsOk) {
  std::string body = R"({"command":"plugins","params":{}})";
  std::string resp;
  bool ok = send_http_post("127.0.0.1", 8555, "/rpc", body, resp);
  ASSERT_TRUE(ok);
  json j = json::parse(resp);
  ASSERT_TRUE(j.contains("ok"));
  ASSERT_TRUE(j["ok"].get<bool>());
  ASSERT_TRUE(j.contains("plugins"));
  ASSERT_TRUE(j["plugins"].is_array());
}
