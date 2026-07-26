# See LICENSE file in the project root for license information.

# library name

set(LIBRARY_COMPONENT "webtty")
set(LIBRARY_NAME "${PROJECT_NAME}-lib-${LIBRARY_COMPONENT}")

# library sources
file(GLOB_RECURSE SOURCES "*.cpp")
file(GLOB_RECURSE HEADERS "*.hpp")

# generated sources

set(proto_path ${CMAKE_CURRENT_SOURCE_DIR}/definitions)
file(GLOB_RECURSE proto_files ${proto_path}/*.proto)

set(GENERATED_SOURCE_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated-cpp-sources")
file(MAKE_DIRECTORY ${GENERATED_SOURCE_DIR})

set(GENERATED_SOURCES "")
set(GENERATED_HEADERS "")

foreach(proto_file ${proto_files})
  string(REPLACE ${proto_path} ${GENERATED_SOURCE_DIR} proto_file ${proto_file})
  string(REPLACE ".proto" ".pb.cc" proto_cpp_src ${proto_file})
  list(APPEND GENERATED_SOURCES ${proto_cpp_src})
  string(REPLACE ".proto" ".pb.h" proto_hdr_src ${proto_file})
  list(APPEND GENERATED_SOURCES ${proto_hdr_src})
  list(APPEND GENERATED_HEADERS ${proto_hdr_src})
endforeach()

add_custom_command(
  OUTPUT ${GENERATED_SOURCES}
  DEPENDS ${proto_files}
  COMMAND
    ${Protobuf_PROTOC_EXECUTABLE}
    --proto_path=${proto_path}
    --cpp_out=${GENERATED_SOURCE_DIR}
    ${proto_files}
  VERBATIM)

# library definition

add_library(${LIBRARY_NAME} ${SOURCES} ${HEADERS} ${GENERATED_SOURCES})
add_library(${PROJECT_NAME}::${LIBRARY_COMPONENT} ALIAS ${LIBRARY_NAME})

if(ENABLE_STRICT_WARNINGS)
  if(MSVC)
    set_source_files_properties(${GENERATED_SOURCES} PROPERTIES COMPILE_OPTIONS /W0)
  else()
    set_source_files_properties(${GENERATED_SOURCES} PROPERTIES COMPILE_OPTIONS -w)
  endif()
endif()

if(ENABLE_STRICT_WARNINGS AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  # GCC 13 reports Boost.Asio's type-erased executor storage as potentially
  # uninitialized while instantiating the Beast websocket read operation.
  set_source_files_properties(
    client.cpp
    server.cpp
    PROPERTIES COMPILE_OPTIONS -Wno-maybe-uninitialized)
endif()

set_target_properties(
  ${LIBRARY_NAME}
  PROPERTIES
  VERSION ${PROJECT_VERSION}
  OUTPUT_NAME ${PROJECT_NAME}-${LIBRARY_COMPONENT}
  EXPORT_NAME ${LIBRARY_COMPONENT}
  SOVERSION ${SOVERSION}
  INTERFACE_${PROJECT_NAME}_MAJOR_VERSION ${SOVERSION}
  APPEND PROPERTY COMPATIBLE_INTERFACE_STRING ${PROJECT_NAME}_MAJOR_VERSION)

if(INSTALL_RPATH_KEYWORD)
  set_target_properties(${LIBRARY_NAME} PROPERTIES ${INSTALL_RPATH_KEYWORD} ${RPATH_LIBRARY})
endif()

target_include_directories(
  ${LIBRARY_NAME}
  PUBLIC
  $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/bin/webtty/lib/>
  PRIVATE
  $<BUILD_INTERFACE:${GENERATED_SOURCE_DIR}>/)

target_link_libraries(
  ${LIBRARY_NAME}
  PUBLIC
  Threads::Threads
  Boost::boost
  Boost::filesystem
  ${PROJECT_NAME}::core
  ${PROJECT_NAME}::io
  PRIVATE
  ${SSL_LIBRARIES}
  protobuf::libprotobuf)

# install the library

install(TARGETS ${LIBRARY_NAME}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

# Xcode source group

if(${CMAKE_GENERATOR} STREQUAL Xcode)
  get_target_property(TARGET_SOURCES ${LIBRARY_NAME} SOURCES)
  list(REMOVE_ITEM TARGET_SOURCES ${GENERATED_SOURCES})
  source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${TARGET_SOURCES})
endif()
