#include "instrument-script-server/client/instrument-server-client.hpp"

#include <grpcpp/grpcpp.h>
#include <gtest/gtest.h>
#include <thread>

using namespace instserver::daemon;
namespace v1 = instserver::daemon::v1;

class TestService final : public v1::DaemonService::Service {
public:
  bool fail_next = false;

  grpc::Status maybe_fail() {
    if (fail_next) {
      fail_next = false;
      return {grpc::StatusCode::INTERNAL, "forced failure"};
    }
    return grpc::Status::OK;
  }

  grpc::Status DaemonStatus(grpc::ServerContext *,
                            const v1::DaemonStatusRequest *,
                            v1::DaemonStatusResponse *resp) override {

    auto status = maybe_fail();
    if (!status.ok()) {
      return status;
    }

    resp->mutable_standard_response()->set_ok(true);
    resp->set_running(true);
    resp->set_pid(777);
    return grpc::Status::OK;
  }

  grpc::Status JobList(grpc::ServerContext *, const v1::JobListRequest *,
                       v1::JobListResponse *resp) override {

    auto status = maybe_fail();
    if (!status.ok()) {
      return status;
    }

    resp->mutable_standard_response()->set_ok(true);

    auto &jobs = *resp->mutable_jobs();

    v1::Job job_value;
    job_value.set_status(v1::JOB_STATUS_COMPLETED);

    jobs[42] = job_value;

    return grpc::Status::OK;
  }

  grpc::Status StopDaemon(grpc::ServerContext *, const v1::DaemonStop *,
                          v1::StandardResponse *resp) override {

    auto status = maybe_fail();
    if (!status.ok()) {
      return status;
    }

    resp->set_ok(true);
    return grpc::Status::OK;
  }
};

/* ========================= */
/* Test Fixture */
/* ========================= */

class ClientTest : public ::testing::Test {
protected:
  std::unique_ptr<grpc::Server> server;
  std::unique_ptr<TestService> service;
  std::thread server_thread;
  int port = 0;

  std::unique_ptr<instserver::client::InstrumentServerClient> client;

  void SetUp() override {
    service = std::make_unique<TestService>();

    grpc::ServerBuilder builder;

    builder.AddListeningPort("127.0.0.1:0", grpc::InsecureServerCredentials(),
                             &port);

    builder.RegisterService(service.get());

    server = builder.BuildAndStart();
    ASSERT_NE(server, nullptr);
    ASSERT_GT(port, 0);

    server_thread = std::thread([this]() { server->Wait(); });

    client = std::make_unique<instserver::client::InstrumentServerClient>(
        static_cast<uint16_t>(port));
  }

  void TearDown() override {
    client.reset();

    if (server) {
      server->Shutdown();
    }

    if (server_thread.joinable()) {
      server_thread.join();
    }
  }
};

/* ========================= */
/* Happy Path Tests */
/* ========================= */

TEST_F(ClientTest, DaemonStatus) {
  v1::DaemonStatusRequest req;

  auto resp = client->daemon_status(req);

  ASSERT_TRUE(resp.standard_response().ok());
  ASSERT_TRUE(resp.running());
  ASSERT_EQ(resp.pid(), 777);
}

TEST_F(ClientTest, JobList) {
  v1::JobListRequest req;

  auto resp = client->job_list(req);

  ASSERT_TRUE(resp.standard_response().ok());

  ASSERT_EQ(resp.jobs().size(), 1U);

  const auto &[key, job] = *resp.jobs().begin();
  ASSERT_EQ(key, 42U);
  ASSERT_EQ(job.status(), v1::JOB_STATUS_COMPLETED);
}

TEST_F(ClientTest, StopDaemon) {
  v1::DaemonStop req;

  auto resp = client->stop_daemon(req);

  ASSERT_TRUE(resp.ok());
}

/* ========================= */
/* Error / Edge Cases */
/* ========================= */

TEST_F(ClientTest, GrpcFailure) {
  service->fail_next = true;

  v1::JobListRequest req;

  EXPECT_THROW({ auto resp = client->job_list(req); }, std::runtime_error);
}

TEST_F(ClientTest, MultipleCalls) {
  for (int i = 0; i < 10; i++) {
    v1::DaemonStatusRequest req;
    auto resp = client->daemon_status(req);

    ASSERT_TRUE(resp.standard_response().ok());
  }
}
