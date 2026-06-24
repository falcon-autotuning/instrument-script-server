cmake_minimum_required(VERSION 3.20)

include(GNUInstallDirs)

find_package(Protobuf REQUIRED)
find_package(gRPC CONFIG REQUIRED)

set(PROTO_ROOT ${CMAKE_CURRENT_SOURCE_DIR})

set(PROTO_FILES
  ${PROTO_ROOT}/instserver/server/v1/daemon_messages.proto
)

# ============================================================
# C++ PROTO TARGET
# ============================================================

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
# PROTOBUF-C (HYBRID MODE)
# ============================================================

if(BUILD_C_CLIENT)

  find_package(protobuf-c CONFIG REQUIRED)

  # ----------------------------------------
  # Option: force regeneration (CI / dev)
  # ----------------------------------------
  option(REGENERATE_PROTO_C "Regenerate protobuf-c sources" OFF)

  # Default: Linux regenerates, Windows consumes
  if(NOT WIN32)
    set(REGENERATE_PROTO_C ON CACHE BOOL "" FORCE)
  endif()

  # ----------------------------------------
  # Paths
  # ----------------------------------------
  set(PROTO_C_OUT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/c)

  set(GENERATED_C_SRCS
    ${PROTO_C_OUT_DIR}/instserver/server/v1/daemon_messages.pb-c.c
    ${PROTO_C_OUT_DIR}/google/protobuf/timestamp.pb-c.c
  )

  set(GENERATED_C_HDRS
    ${PROTO_C_OUT_DIR}/instserver/server/v1/daemon_messages.pb-c.h
    ${PROTO_C_OUT_DIR}/google/protobuf/timestamp.pb-c.h
  )

  # ----------------------------------------
  # Optional regeneration step
  # ----------------------------------------
  if(REGENERATE_PROTO_C)

    message(STATUS "Protobuf-C: regeneration ENABLED")

    find_program(PROTOC_GEN_C protoc-gen-c)
    if(NOT PROTOC_GEN_C)
      message(FATAL_ERROR
        "protoc-gen-c not found but regeneration requested.\n"
        "Install protobuf-c compiler tools or disable REGENERATE_PROTO_C."
      )
    endif()

    add_custom_command(
      OUTPUT ${GENERATED_C_SRCS} ${GENERATED_C_HDRS}

      COMMAND ${CMAKE_COMMAND} -E make_directory ${PROTO_C_OUT_DIR}

      COMMAND ${Protobuf_PROTOC_EXECUTABLE}
        --proto_path=${PROTO_ROOT}
        --proto_path=${Protobuf_INCLUDE_DIRS}
        --plugin=protoc-gen-c=${PROTOC_GEN_C}
        --c_out=${PROTO_C_OUT_DIR}
        instserver/server/v1/daemon_messages.proto
        google/protobuf/timestamp.proto

      DEPENDS
        ${PROTO_FILES}
        ${Protobuf_INCLUDE_DIRS}/google/protobuf/timestamp.proto

      COMMENT "Generating protobuf-c sources"
      VERBATIM
    )

    add_custom_target(generate-proto-c
      DEPENDS ${GENERATED_C_SRCS} ${GENERATED_C_HDRS}
    )

  else()

    message(STATUS "Protobuf-C: using pre-generated sources")

    # Optional sanity check (helps avoid silent failures)
    foreach(file ${GENERATED_C_SRCS} ${GENERATED_C_HDRS})
      if(NOT EXISTS ${file})
        message(FATAL_ERROR
          "Missing generated protobuf-c file:\n  ${file}\n"
          "Run a Linux build or enable REGENERATE_PROTO_C."
        )
      endif()
    endforeach()

  endif()

  # ----------------------------------------
  # Library
  # ----------------------------------------
  add_library(instserver-proto-c STATIC ${GENERATED_C_SRCS})

  if(REGENERATE_PROTO_C)
    add_dependencies(instserver-proto-c generate-proto-c)
  endif()

  target_include_directories(instserver-proto-c
    PUBLIC
      $<BUILD_INTERFACE:${PROTO_C_OUT_DIR}>
      $<INSTALL_INTERFACE:include>
  )

  target_link_libraries(instserver-proto-c
    PUBLIC protobuf-c::protobuf-c
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

if(BUILD_C_CLIENT)
  install(
    DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/c/
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
