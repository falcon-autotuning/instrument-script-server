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

  find_package(protobuf-c CONFIG REQUIRED)

  # Locate protoc-gen-c (cross-platform safe)
  find_program(PROTOC_GEN_C protoc-gen-c
    REQUIRED
    HINTS "${_VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/tools/protobuf-c"
  )

  function(protobuf_generate_c TARGET OUT_DIR)
    cmake_parse_arguments(ARG "" "" "PROTOS" ${ARGN})

    if(NOT ARG_PROTOS)
      message(FATAL_ERROR "protobuf_generate_c requires PROTOS")
    endif()

    set(generated_srcs "")
    set(generated_hdrs "")

    foreach(proto ${ARG_PROTOS})
      get_filename_component(proto_name ${proto} NAME_WE)

      if(proto MATCHES "^${PROTO_ROOT}")
        file(RELATIVE_PATH rel_path ${PROTO_ROOT} ${proto})
      else()
        file(RELATIVE_PATH rel_path ${Protobuf_INCLUDE_DIRS} ${proto})
      endif()
      get_filename_component(rel_dir ${rel_path} DIRECTORY)

      set(c_file "${OUT_DIR}/${rel_dir}/${proto_name}.pb-c.c")
      set(h_file "${OUT_DIR}/${rel_dir}/${proto_name}.pb-c.h")

      list(APPEND generated_srcs ${c_file})
      list(APPEND generated_hdrs ${h_file})

      add_custom_command(
        OUTPUT ${c_file} ${h_file}

        COMMAND ${CMAKE_COMMAND} -E make_directory ${OUT_DIR}/${rel_dir}

        COMMAND ${Protobuf_PROTOC_EXECUTABLE}
          --proto_path=${PROTO_ROOT}
          --proto_path=${Protobuf_INCLUDE_DIRS}
          --plugin=protoc-gen-c=${PROTOC_GEN_C}
          --c_out=${OUT_DIR}
          ${rel_path}

        DEPENDS ${proto}

        COMMENT "Generating protobuf-c: ${rel_path}"
        VERBATIM
      )
    endforeach()

    target_sources(${TARGET} PRIVATE ${generated_srcs})
    target_include_directories(${TARGET}
      PUBLIC
        $<BUILD_INTERFACE:${OUT_DIR}>
        $<INSTALL_INTERFACE:include>
    )
  endfunction()

  set(PROTO_C_OUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/c)

  add_library(instserver-proto-c STATIC)
  add_dependencies(instserver-proto-c instserver-proto) # optional ordering

  protobuf_generate_c(
    instserver-proto-c
    ${PROTO_C_OUT_DIR}
    PROTOS
      ${PROTO_FILES}
      ${Protobuf_INCLUDE_DIRS}/google/protobuf/timestamp.proto
  )

  target_link_libraries(instserver-proto-c
    PRIVATE protobuf-c::protobuf-c
  )

endif()

install(
  DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/instserver
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
  FILES_MATCHING
    PATTERN "*.pb.h"
    PATTERN "*.grpc.pb.h"
)

if(BUILD_C_CLIENT)
  install(
    DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/c/
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
