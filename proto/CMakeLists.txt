add_library(instserver-proto STATIC)

set(PROTO_ROOT ${CMAKE_CURRENT_SOURCE_DIR})

set(PROTO_FILES
  ${PROTO_ROOT}/instserver/server/v1/daemon_messages.proto
)

target_sources(instserver-proto PRIVATE ${PROTO_FILES})

set_target_properties(instserver-proto
  PROPERTIES POSITION_INDEPENDENT_CODE ON
)

target_include_directories(instserver-proto
  PUBLIC
    $<BUILD_INTERFACE:${PROTO_ROOT}>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(instserver-proto
  PUBLIC protobuf::libprotobuf
)

protobuf_generate(
  TARGET instserver-proto
  LANGUAGE cpp
  IMPORT_DIRS ${PROTO_ROOT}
  PROTOS ${PROTO_FILES}
)

install(
  DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/instserver
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
  FILES_MATCHING PATTERN "*.pb.h"
)

install(TARGETS instserver-proto
  EXPORT instrument-script-server-targets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
