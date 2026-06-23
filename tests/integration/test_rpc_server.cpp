#include "instrument-script-server/plugin/PluginRegistry.hpp"
#include "instrument-script-server/server/ServerDaemon.hpp"
#include <gtest/gtest.h>
#include <grpcpp/grpcpp.h>
#include "instserver/server/v1/daemon_messages.grpc.pb.h"
#include <chrono>
#include <memory>
#include <thread>

using namespace instserver::server;

class RpcServerTest : public ::testing::Test {
protected:
  void SetUp() override {
    rpc_host_ = "127.0.0.1";
    rpc_port_ = 8555;

    auto &daemon = instserver::ServerDaemon::instance();

    if (daemon.is_running()) {
      daemon.stop();
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    daemon.set_rpc_port(rpc_port_);
    if (!daemon.start()) {
      GTEST_SKIP() << "Failed to start daemon";
      return;
    }
    started_daemon_ = true;
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Poll the gRPC channel until connected or timeout
    std::string server_address = rpc_host_ + ":" + std::to_string(rpc_port_);
    auto channel = grpc::CreateChannel(server_address, grpc::InsecureChannelCredentials());
    auto stub = v1::DaemonService::NewStub(channel);

    const int timeout_ms = 5000;
    const int poll_interval_ms = 100;
    int waited = 0;
    bool ready = false;
    while (waited < timeout_ms) {
      grpc::ClientContext context;
      v1::ListInstrumentsRequest req;
      v1::ListInstrumentsResponse resp;
      grpc::Status status = stub->ListInstruments(&context, req, &resp);
      if (status.ok()) {
        ready = true;
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(poll_interval_ms));
      waited += poll_interval_ms;
    }

    if (!ready) {
      if (started_daemon_) {
        daemon.stop();
      }
      FAIL() << "gRPC server not responding on " << server_address
             << " after " << timeout_ms << "ms.";
    }
  }

  void TearDown() override {
    auto &daemon = instserver::ServerDaemon::instance();
    auto &plugin_registry = instserver::plugin::PluginRegistry::instance();
    plugin_registry.unload_all();
    if (daemon.is_running()) {
      daemon.stop();
      std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
  }

  std::string rpc_host_;
  int rpc_port_{0};
  bool started_daemon_{false};
};

TEST_F(RpcServerTest, ListReturnsOk) {
  std::string server_address = rpc_host_ + ":" + std::to_string(rpc_port_);
  auto channel = grpc::CreateChannel(server_address, grpc::InsecureChannelCredentials());
  auto stub = v1::DaemonService::NewStub(channel);

  grpc::ClientContext context;
  v1::ListInstrumentsRequest req;
  v1::ListInstrumentsResponse resp;
  grpc::Status status = stub->ListInstruments(&context, req, &resp);

  ASSERT_TRUE(status.ok());
  ASSERT_TRUE(resp.standard_response().ok());
}

TEST_F(RpcServerTest, PluginsReturnsOk) {
  std::string server_address = rpc_host_ + ":" + std::to_string(rpc_port_);
  auto channel = grpc::CreateChannel(server_address, grpc::InsecureChannelCredentials());
  auto stub = v1::DaemonService::NewStub(channel);

  grpc::ClientContext context;
  v1::DiscoverRequest req;
  v1::DiscoverResponse resp;
  grpc::Status status = stub->Discover(&context, req, &resp);

  ASSERT_TRUE(status.ok());
  ASSERT_TRUE(resp.standard_response().ok());
}
