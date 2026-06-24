#include "instrument-script-server/instrument-script-server-client.h"
#include "instserver/server/v1/daemon_messages.grpc.pb.h"

#include <grpcpp/grpcpp.h>
#include <gtest/gtest.h>
#include <thread>

using namespace instserver::server;

class TestService final : public v1::DaemonService::Service {
public:
  bool fail_next = false;

  grpc::Status maybe_fail() {
    if (fail_next) {
      fail_next = false;
      return grpc::Status(grpc::StatusCode::INTERNAL, "forced failure");
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

  instrument_server_client_t *client = nullptr;

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

    client = instrument_server_client_create((uint16_t)port);
    ASSERT_NE(client, nullptr);
  }

  void TearDown() override {
    if (client != nullptr) {
      instrument_server_client_destroy(client);
      client = nullptr;
    }

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
  Instserver__Server__V1__DaemonStatusRequest req =
      INSTSERVER__SERVER__V1__DAEMON_STATUS_REQUEST__INIT;

  Instserver__Server__V1__DaemonStatusResponse *resp = nullptr;

  int rc = instrument_server_client_daemon_status(client, &req, &resp);

  ASSERT_EQ(rc, 0);
  ASSERT_NE(resp, nullptr);

  ASSERT_TRUE(resp->standard_response->ok);
  ASSERT_TRUE(resp->running);
  ASSERT_EQ(resp->pid, 777);

  instrument_server_client_free_response(resp);
}

TEST_F(ClientTest, JobList) {
  Instserver__Server__V1__JobListRequest req =
      INSTSERVER__SERVER__V1__JOB_LIST_REQUEST__INIT;

  Instserver__Server__V1__JobListResponse *resp = nullptr;

  int rc = instrument_server_client_job_list(client, &req, &resp);

  ASSERT_EQ(rc, 0);
  ASSERT_NE(resp, nullptr);

  ASSERT_TRUE(resp->standard_response->ok);
  ASSERT_EQ(resp->n_jobs, 1u);
  ASSERT_EQ(resp->jobs[0]->key, 42u);

  instrument_server_client_free_response(resp);
}

TEST_F(ClientTest, StopDaemon) {
  Instserver__Server__V1__DaemonStop req =
      INSTSERVER__SERVER__V1__DAEMON_STOP__INIT;

  Instserver__Server__V1__StandardResponse *resp = nullptr;

  int rc = instrument_server_client_stop_daemon(client, &req, &resp);

  ASSERT_EQ(rc, 0);
  ASSERT_NE(resp, nullptr);

  ASSERT_TRUE(resp->ok);

  instrument_server_client_free_response(resp);
}

/* ========================= */
/* Error / Edge Cases */
/* ========================= */

TEST(ClientEdgeCases, NullInputs) {
  Instserver__Server__V1__DaemonStatusRequest req =
      INSTSERVER__SERVER__V1__DAEMON_STATUS_REQUEST__INIT;

  EXPECT_NE(instrument_server_client_daemon_status(NULL, &req, NULL), 0);
}

TEST(ClientEdgeCases, NullMatrix) {
  Instserver__Server__V1__DaemonStatusRequest req =
      INSTSERVER__SERVER__V1__DAEMON_STATUS_REQUEST__INIT;

  Instserver__Server__V1__DaemonStatusResponse *resp = nullptr;

  EXPECT_NE(instrument_server_client_daemon_status(NULL, &req, &resp), 0);
  EXPECT_NE(instrument_server_client_daemon_status(
                (instrument_server_client_t *)1, NULL, &resp),
            0);
  EXPECT_NE(instrument_server_client_daemon_status(
                (instrument_server_client_t *)1, &req, NULL),
            0);
}

TEST_F(ClientTest, GrpcFailure) {
  service->fail_next = true;

  Instserver__Server__V1__JobListRequest req =
      INSTSERVER__SERVER__V1__JOB_LIST_REQUEST__INIT;

  Instserver__Server__V1__JobListResponse *resp = nullptr;

  int rc = instrument_server_client_job_list(client, &req, &resp);

  EXPECT_NE(rc, 0);
  EXPECT_EQ(resp, nullptr);
}

TEST_F(ClientTest, FreeNullIsSafe) {
  instrument_server_client_free_response(nullptr);
}

TEST_F(ClientTest, MultipleCalls) {
  for (int i = 0; i < 10; i++) {
    Instserver__Server__V1__DaemonStatusRequest req =
        INSTSERVER__SERVER__V1__DAEMON_STATUS_REQUEST__INIT;

    Instserver__Server__V1__DaemonStatusResponse *resp = nullptr;

    ASSERT_EQ(instrument_server_client_daemon_status(client, &req, &resp), 0);

    instrument_server_client_free_response(resp);
  }
}
