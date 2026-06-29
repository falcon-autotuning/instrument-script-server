cmake_minimum_required(VERSION 3.20)

include(GNUInstallDirs)

find_package(Protobuf REQUIRED)
find_package(gRPC CONFIG REQUIRED)

set(PROTO_ROOT ${CMAKE_CURRENT_SOURCE_DIR})

set(PROTO_FILES
  ${PROTO_ROOT}/instserver/server/v1/daemon_messages.proto
)

add_library(instserver-proto STATIC)

set_target_properties(instserver-proto PROPERTIES
  POSITION_INDEPENDENT_CODE ON
)

target_sources(instserver-proto PRIVATE ${PROTO_FILES})

target_include_directories(instserver-proto
  PUBLIC
    $<BUILD_INTERFACE:${PROTO_ROOT}>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(instserver-proto
  PUBLIC
    protobuf::libprotobuf
    gRPC::grpc++
)

protobuf_generate(
  TARGET instserver-proto
  LANGUAGE cpp
  IMPORT_DIRS ${PROTO_ROOT}
  PROTOS ${PROTO_FILES}
)

protobuf_generate(
  TARGET instserver-proto
  LANGUAGE grpc
  GENERATE_EXTENSIONS .grpc.pb.h .grpc.pb.cc
  PLUGIN "protoc-gen-grpc=$<TARGET_FILE:gRPC::grpc_cpp_plugin>"
  IMPORT_DIRS ${PROTO_ROOT}
  PROTOS ${PROTO_FILES}
)

# ============================================================
# INSTALL
# ============================================================

install(
  DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/instserver
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
  FILES_MATCHING
    PATTERN "*.pb.h"
    PATTERN "*.grpc.pb.h"
)

install(TARGETS instserver-proto
  EXPORT instrument-script-server-targets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

