#include "instrument-server/server/ServerDaemon.hpp"
#include <gtest/gtest.h>
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
#include <unistd.h>
#endif

#include <chrono>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>

using json = nlohmann::json;

static bool send_http_post(const std::string &host, int port,
                           const std::string &path, const std::string &body,
                           std::string &out_response_body,
                           int timeout_ms = 5000) {
#ifdef _WIN32
  WSADATA wsa_data;
  if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
    return false;
  }
#endif

  int sockfd = socket(AF_INET, SOCK_STREAM, 0);
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
    WSACleanup();
#else
    close(sockfd);
#endif
    return false;
  }

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

class RpcJobsTest : public ::testing::Test {
protected:
  void SetUp() override {
    auto &daemon = instserver::ServerDaemon::instance();
    if (!daemon.is_running()) {
      daemon.set_rpc_port(8555);
      ASSERT_TRUE(daemon.start());
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
  }
};

TEST_F(RpcJobsTest, SubmitSleepJobAndPoll) {
  // Submit a sleep job (100 ms)
  json req;
  req["command"] = "submit_job";
  req["params"] = json::object();
  req["params"]["job_type"] = "sleep";
  req["params"]["params"] = json::object();
  req["params"]["params"]["duration_ms"] = 200;

  std::string resp_body;
  ASSERT_TRUE(send_http_post("127.0.0.1", 8555, "/rpc", req.dump(), resp_body));
  json r = json::parse(resp_body);
  ASSERT_TRUE(r.value("ok", false));
  std::string job_id = r.value("job_id", "");
  ASSERT_FALSE(job_id.empty());

  // Poll for status until completed
  std::string status;
  bool completed = false;
  for (int i = 0; i < 50; ++i) {
    json sreq;
    sreq["command"] = "job_status";
    sreq["params"] = json::object();
    sreq["params"]["job_id"] = job_id;
    std::string sb;
    ASSERT_TRUE(send_http_post("127.0.0.1", 8555, "/rpc", sreq.dump(), sb));
    json sr = json::parse(sb);
    ASSERT_TRUE(sr.value("ok", false));
    status = sr.value("status", "");
    if (status == "completed" || status == "failed" || status == "canceled") {
      completed = true;
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  ASSERT_TRUE(completed);

  // Get result
  json rreq;
  rreq["command"] = "job_result";
  rreq["params"] = json::object();
  rreq["params"]["job_id"] = job_id;
  std::string rb;
  ASSERT_TRUE(send_http_post("127.0.0.1", 8555, "/rpc", rreq.dump(), rb));
  json rr = json::parse(rb);
  ASSERT_TRUE(rr.value("ok", false));
  ASSERT_TRUE(rr.contains("result"));
  ASSERT_EQ(rr["result"].value("message", ""), "slept");
}
