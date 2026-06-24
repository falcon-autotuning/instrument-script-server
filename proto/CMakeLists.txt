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

if(BUILD_C_CLIENT)

  find_package(PkgConfig REQUIRED)
  pkg_check_modules(PROTOBUF_C REQUIRED libprotobuf-c)

  set(PROTO_C_OUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/c)
  file(MAKE_DIRECTORY ${PROTO_C_OUT_DIR})

  set(PROTO_BASENAME instserver/server/v1/daemon_messages)

  set(PROTO_C_SRCS
    ${PROTO_C_OUT_DIR}/${PROTO_BASENAME}.pb-c.c
    ${PROTO_C_OUT_DIR}/google/protobuf/timestamp.pb-c.c
  )

  set(PROTO_C_HDRS
    ${PROTO_C_OUT_DIR}/${PROTO_BASENAME}.pb-c.h
    ${PROTO_C_OUT_DIR}/google/protobuf/timestamp.pb-c.h
  )

  add_custom_command(
    OUTPUT ${PROTO_C_SRCS} ${PROTO_C_HDRS}

    COMMAND ${Protobuf_PROTOC_EXECUTABLE}
        --proto_path=${PROTO_ROOT}
        --proto_path=${Protobuf_INCLUDE_DIRS}
        --c_out=${PROTO_C_OUT_DIR}
        instserver/server/v1/daemon_messages.proto
        google/protobuf/timestamp.proto

    DEPENDS ${PROTO_FILES}

    COMMENT "Generating protobuf-c sources"
    VERBATIM
  )

  add_custom_target(generate-proto-c
    DEPENDS ${PROTO_C_SRCS} ${PROTO_C_HDRS}
  )

  add_library(instserver-proto-c STATIC ${PROTO_C_SRCS})

  add_dependencies(instserver-proto-c generate-proto-c)

  set_target_properties(instserver-proto-c PROPERTIES
    POSITION_INDEPENDENT_CODE ON
  )

  target_include_directories(instserver-proto-c
    PUBLIC
      $<BUILD_INTERFACE:${PROTO_C_OUT_DIR}>
      $<INSTALL_INTERFACE:include>
    PRIVATE
      ${PROTOBUF_C_INCLUDE_DIRS}
  )

  target_link_libraries(instserver-proto-c
    PRIVATE ${PROTOBUF_C_LIBRARIES}
  )

  target_compile_options(instserver-proto-c
    PRIVATE ${PROTOBUF_C_CFLAGS_OTHER}
  )

endif()

install(
  DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/instserver
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
  FILES_MATCHING
    PATTERN "*.pb.h"
    PATTERN "*.grpc.pb.h"
)

# Only install protobuf-c headers if enabled
if(BUILD_C_CLIENT)
  install(
    DIRECTORY ${PROTO_C_OUT_DIR}/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    FILES_MATCHING PATTERN "*.pb-c.h"
  )
endif()

install(TARGETS instserver-proto
  EXPORT instrument-script-server-targets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

if(BUILD_C_CLIENT)
  install(TARGETS instserver-proto-c
    EXPORT instrument-script-server-targets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  )
endif()
