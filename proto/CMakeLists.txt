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

  include(FetchContent)

  # Build protobuf-c from source (only for protoc-gen-c tool)
  FetchContent_Declare(
    protobuf_c_src
    GIT_REPOSITORY https://github.com/protobuf-c/protobuf-c.git
    GIT_TAG v1.5.2
  )

  # Try system first
  find_program(PROTOC_GEN_C protoc-gen-c)

  # Only build fallback if missing
  if(WIN32 AND NOT PROTOC_GEN_C)
    FetchContent_MakeAvailable(protobuf_c_src)

    # protoc-gen-c is in this source file in protobuf-c repo
    add_executable(protoc-gen-c
    ${protobuf_c_src_SOURCE_DIR}/protoc-gen-c/c_generator.c
    ${protobuf_c_src_SOURCE_DIR}/protoc-gen-c/c_helpers.c
    ${protobuf_c_src_SOURCE_DIR}/protoc-gen-c/c_field.cc
    ${protobuf_c_src_SOURCE_DIR}/protoc-gen-c/c_file.cc
    ${protobuf_c_src_SOURCE_DIR}/protoc-gen-c/c_message.cc
    ${protobuf_c_src_SOURCE_DIR}/protoc-gen-c/c_service.cc
  )

    target_include_directories(protoc-gen-c PRIVATE
    ${protobuf_c_src_SOURCE_DIR}
    ${protobuf_c_src_SOURCE_DIR}/protobuf-c
  )

    target_link_libraries(protoc-gen-c PRIVATE
    protobuf::libprotobuf
  )

    # now we can use it
    set(PROTOC_GEN_C $<TARGET_FILE:protoc-gen-c>)

  endif()

  # Fallback to built target
  if(NOT PROTOC_GEN_C)
    if(TARGET protoc-gen-c)
      set(PROTOC_GEN_C $<TARGET_FILE:protoc-gen-c>)
    elseif(TARGET protobuf-c::protoc-gen-c)
      set(PROTOC_GEN_C $<TARGET_FILE:protobuf-c::protoc-gen-c>)
    endif()
  endif()

  if(NOT PROTOC_GEN_C)
    message(FATAL_ERROR "Failed to locate or build protoc-gen-c")
  endif()

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

      set(_plugin_dep "")
      if(TARGET protoc-gen-c)
        set(_plugin_dep protoc-gen-c)
      else()
        set(_plugin_dep "")
      endif()

      add_custom_command(
        OUTPUT ${c_file} ${h_file}

        COMMAND ${CMAKE_COMMAND} -E make_directory ${OUT_DIR}/${rel_dir}

        COMMAND ${Protobuf_PROTOC_EXECUTABLE}
          --proto_path=${PROTO_ROOT}
          --proto_path=${Protobuf_INCLUDE_DIRS}
          --plugin=protoc-gen-c=${PROTOC_GEN_C}
          --c_out=${OUT_DIR}
          ${rel_path}

        DEPENDS
          ${proto}
          ${_plugin_dep}

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
  if(TARGET protoc-gen-c)
    add_dependencies(instserver-proto-c protoc-gen-c)
  elseif(TARGET protobuf-c::protoc-gen-c)
    add_dependencies(instserver-proto-c protobuf-c::protoc-gen-c)
  endif()

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
