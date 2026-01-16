#include "instrument-server/server/ServerDaemon.hpp"
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#ifdef _WIN32
#include "instrument-server/compat/WinSock.hpp"
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <chrono>
#include <fstream>
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

class RpcMeasureQueueTest : public ::testing::Test {
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

// This test creates a trivial Lua script that does no instrument calls but
// verifies measure job enqueueing: both jobs should be accepted quickly and
// both should complete (even though they don't touch instruments).
TEST_F(RpcMeasureQueueTest, EnqueueTwoMeasureJobsQuickly) {
  // Create two tiny scripts
  std::string s1 = "print('script1 start')\nprint('script1 end')\n";
  std::string s2 = "print('script2 start')\nprint('script2 end')\n";

  std::string path1 = "tests/tmp_script1.lua";
  std::string path2 = "tests/tmp_script2.lua";
  {
    std::ofstream f(path1);
    f << s1;
  }
  {
    std::ofstream f(path2);
    f << s2;
  }

  json req1;
  req1["command"] = "submit_measure";
  req1["params"] = json::object();
  req1["params"]["script_path"] = path1;

  std::string r1s;
  ASSERT_TRUE(send_http_post("127.0.0.1", 8555, "/rpc", req1.dump(), r1s));
  json r1 = json::parse(r1s);
  ASSERT_TRUE(r1.value("ok", false));
  std::string job1 = r1.value("job_id", "");
  ASSERT_FALSE(job1.empty());

  // Immediately submit second
  json req2;
  req2["command"] = "submit_measure";
  req2["params"] = json::object();
  req2["params"]["script_path"] = path2;

  std::string r2s;
  ASSERT_TRUE(send_http_post("127.0.0.1", 8555, "/rpc", req2.dump(), r2s));
  json r2 = json::parse(r2s);
  ASSERT_TRUE(r2.value("ok", false));
  std::string job2 = r2.value("job_id", "");
  ASSERT_FALSE(job2.empty());

  // Both jobs should be present in job list quickly
  json lreq;
  lreq["command"] = "job_list";
  lreq["params"] = json::object();
  std::string ls;
  ASSERT_TRUE(send_http_post("127.0.0.1", 8555, "/rpc", lreq.dump(), ls));
  json lj = json::parse(ls);
  ASSERT_TRUE(lj.value("ok", false));
  ASSERT_TRUE(lj.contains("jobs"));

  // Wait for both jobs to complete (poll)
  auto wait_for_completion = [&](const std::string &jobid) -> bool {
    for (int i = 0; i < 100; ++i) {
      json sreq;
      sreq["command"] = "job_status";
      sreq["params"] = json::object();
      sreq["params"]["job_id"] = jobid;
      std::string sb;
      if (!send_http_post("127.0.0.1", 8555, "/rpc", sreq.dump(), sb)) {
        return false;
      }
      json sr = json::parse(sb);
      if (!sr.value("ok", false)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        continue;
      }
      std::string st = sr.value("status", "");
      if (st == "completed")
        return true;
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return false;
  };

  ASSERT_TRUE(wait_for_completion(job1));
  ASSERT_TRUE(wait_for_completion(job2));

  // Fetch results
  json resreq;
  resreq["command"] = "job_result";
  resreq["params"] = json::object();
  resreq["params"]["job_id"] = job1;
  std::string rb1;
  ASSERT_TRUE(send_http_post("127.0.0.1", 8555, "/rpc", resreq.dump(), rb1));
  json rbj1 = json::parse(rb1);
  ASSERT_TRUE(rbj1.value("ok", false));

  resreq["params"]["job_id"] = job2;
  std::string rb2;
  ASSERT_TRUE(send_http_post("127.0.0.1", 8555, "/rpc", resreq.dump(), rb2));
  json rbj2 = json::parse(rb2);
  ASSERT_TRUE(rbj2.value("ok", false));
}
