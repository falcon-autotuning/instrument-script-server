#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <string>

#include "instrument-server/server/ApiRefResolver.hpp"

using namespace instserver::server;

static std::filesystem::path write_temp_file(const std::filesystem::path &p,
                                             const std::string &contents) {
  std::filesystem::create_directories(p.parent_path());
  std::ofstream ofs(p);
  ofs << contents;
  ofs.close();
  return p;
}

TEST(ApiRefResolverTest, ResolvesRelativeToConfigParent) {
  auto tmp = std::filesystem::temp_directory_path() / "instsrv_test_apiref";
  std::filesystem::remove_all(tmp);
  std::filesystem::create_directories(tmp);

  auto cfg_dir = tmp / "configs";
  auto api_dir = tmp / "apis";
  std::filesystem::create_directories(cfg_dir);
  std::filesystem::create_directories(api_dir);

  auto api_file = api_dir / "myapi.yaml";
  auto cfg_file = cfg_dir / "device1.yaml";

  // Minimal API content (content doesn't need to be validated for resolution
  // test)
  write_temp_file(
      api_file, "protocol:\n  type: DUMMY\napi_version: \"1.0\"\ninstrument:\n "
                " name: Dummy\nio: []\ncommands: {}\n");

  // Use a relative api_ref that refers to ../apis/myapi.yaml from configs/
  write_temp_file(cfg_file,
                  "name: TEST1\napi_ref: ../apis/myapi.yaml\nconnection:\n  "
                  "type: VISA\nio_config: {}\n");

  std::string resolved =
      resolve_api_ref("../apis/myapi.yaml", cfg_file.string());
  EXPECT_EQ(std::filesystem::absolute(api_file).string(),
            std::filesystem::absolute(resolved).string());

  // cleanup
  std::filesystem::remove_all(tmp);
}

TEST(ApiRefResolverTest, HandlesFileScheme) {
  auto tmp = std::filesystem::temp_directory_path() / "instsrv_test_apiref2";
  std::filesystem::remove_all(tmp);
  std::filesystem::create_directories(tmp);

  auto api_file = tmp / "myapi2.yaml";
  write_temp_file(
      api_file, "protocol:\n  type: DUMMY\napi_version: \"1.0\"\ninstrument:\n "
                " name: Dummy\nio: []\ncommands: {}\n");

  std::string file_uri;
#ifdef _WIN32
  // Windows absolute path, prefix with file:///
  file_uri =
      std::string("file:///") + std::filesystem::absolute(api_file).string();
#else
  file_uri =
      std::string("file://") + std::filesystem::absolute(api_file).string();
#endif

  // The config path argument can be any existing path; use tmp as config path
  auto cfg_file = tmp / "dummyconfig.yaml";
  write_temp_file(cfg_file, "name: TEST2\napi_ref: " + file_uri +
                                "\nconnection:\n  type: VISA\nio_config: {}\n");

  std::string resolved = resolve_api_ref(file_uri, cfg_file.string());
  EXPECT_EQ(std::filesystem::absolute(api_file).string(),
            std::filesystem::absolute(resolved).string());

  std::filesystem::remove_all(tmp);
}
