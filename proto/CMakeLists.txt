cmake_minimum_required(VERSION 3.20)

include(GNUInstallDirs)

find_package(Protobuf REQUIRED)
find_package(gRPC CONFIG REQUIRED)

set(PROTO_ROOT ${CMAKE_CURRENT_SOURCE_DIR})

set(PROTO_FILES
  ${PROTO_ROOT}/instserver/server/v1/daemon_messages.proto
)

add_library(instserver-proto SHARED)

protobuf_generate(
  LANGUAGE cpp
  IMPORT_DIRS ${PROTO_ROOT}
  PROTOS ${PROTO_FILES}
  OUT_VAR PROTO_SRCS
)

protobuf_generate(
  LANGUAGE grpc
  GENERATE_EXTENSIONS .grpc.pb.h .grpc.pb.cc
  PLUGIN "protoc-gen-grpc=$<TARGET_FILE:gRPC::grpc_cpp_plugin>"
  IMPORT_DIRS ${PROTO_ROOT}
  PROTOS ${PROTO_FILES}
  OUT_VAR GRPC_SRCS
)

target_sources(instserver-proto PRIVATE
  ${PROTO_SRCS}
  ${GRPC_SRCS}
)

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

set_target_properties(instserver-proto PROPERTIES
  POSITION_INDEPENDENT_CODE ON
)

if(WIN32)
  target_compile_definitions(instserver-proto
    PUBLIC PROTOBUF_USE_DLLS
  )
  set_target_properties(instserver-proto PROPERTIES
    WINDOWS_EXPORT_ALL_SYMBOLS ON
  )
endif()

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

