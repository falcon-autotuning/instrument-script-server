#include "instrument-server/server/ServerDaemon.hpp"
#include "instrument-server/Logger.hpp"
#include <gtest/gtest.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "Ws2_32.lib")
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <chrono>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>

// Simple HTTP POST helper
static std::string send_http_request(const std::string &host, int port,
                                     const std::string &body) {
#ifdef _WIN32
  WSADATA wsa_data;
  WSAStartup(MAKEWORD(2, 2), &wsa_data);
#endif

  int sockfd = socket(AF_INET, SOCK_STREAM, 0);
  if (sockfd < 0) {
    return "";
  }

  struct sockaddr_in serv_addr;
  memset(&serv_addr, 0, sizeof(serv_addr));
  serv_addr.sin_family = AF_INET;
  serv_addr.sin_port = htons(static_cast<uint16_t>(port));
  serv_addr.sin_addr.s_addr = inet_addr(host.c_str());

  if (connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
#ifdef _WIN32
    closesocket(sockfd);
    WSACleanup();
#else
    close(sockfd);
#endif
    return "";
  }

  // Set recv timeout to avoid blocking forever
  struct timeval tv;
  tv.tv_sec = 2;  // 2 second timeout
  tv.tv_usec = 0;
#ifdef _WIN32
  setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
#else
  setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif

  // Build HTTP request
  std::ostringstream req;
  req << "POST /rpc HTTP/1.0\r\n";
  req << "Host: " << host << "\r\n";
  req << "Content-Type: application/json\r\n";
  req << "Content-Length: " << body.size() << "\r\n";
  req << "\r\n";
  req << body;

  std::string request = req.str();
  send(sockfd, request.c_str(), request.size(), 0);

  // Read response
  std::string response;
  char buffer[4096];
  int n;
  while ((n = recv(sockfd, buffer, sizeof(buffer) - 1, 0)) > 0) {
    buffer[n] = '\0';
    response += buffer;
  }

#ifdef _WIN32
  closesocket(sockfd);
  WSACleanup();
#else
  close(sockfd);
#endif

  return response;
}

class HttpRpcBasicTest : public ::testing::Test {
protected:
  void SetUp() override {
    // Initialize logger
    instserver::InstrumentLogger::instance().init("http_rpc_test.log",
                                                   spdlog::level::debug);

    auto &daemon = instserver::ServerDaemon::instance();

    // Stop any existing daemon
    if (daemon.is_running()) {
      daemon.stop();
      std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    // Start daemon with RPC
    daemon.set_rpc_port(8556);
    ASSERT_TRUE(daemon.start());

    // Give server time to start
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
  }

  void TearDown() override {
    auto &daemon = instserver::ServerDaemon::instance();
    if (daemon.is_running()) {
      daemon.stop();
    }
  }
};

TEST_F(HttpRpcBasicTest, ServerAcceptsConnection) {
  // Just test that we can connect
  int sockfd = socket(AF_INET, SOCK_STREAM, 0);
  ASSERT_GE(sockfd, 0);

  struct sockaddr_in serv_addr;
  memset(&serv_addr, 0, sizeof(serv_addr));
  serv_addr.sin_family = AF_INET;
  serv_addr.sin_port = htons(8556);
  serv_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

  int result = connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr));
  EXPECT_EQ(result, 0) << "Should be able to connect to RPC server";

#ifdef _WIN32
  closesocket(sockfd);
#else
  close(sockfd);
#endif
}

TEST_F(HttpRpcBasicTest, ServerRespondsToRequest) {
  std::string body = R"({"command":"list","params":{}})";
  std::string response = send_http_request("127.0.0.1", 8556, body);

  EXPECT_FALSE(response.empty()) << "Should receive a response from server";
  EXPECT_NE(response.find("HTTP"), std::string::npos) << "Response should contain HTTP header";
}
