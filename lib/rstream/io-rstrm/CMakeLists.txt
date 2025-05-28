# See LICENSE file in the project root for license information.

# library name

set(LIBRARY_COMPONENT "io-rstrm")
set(LIBRARY_NAME "${PROJECT_NAME}-lib-${LIBRARY_COMPONENT}")

# library sources

file(GLOB_RECURSE SOURCES *.cpp)
file(GLOB_RECURSE HEADERS *.hpp)

# generated sources

set(proto_path ${CMAKE_CURRENT_SOURCE_DIR}/definitions)
file(GLOB_RECURSE proto_files ${proto_path}/*.proto)

set(GENERATED_SOURCE_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated-cpp-sources")
file(MAKE_DIRECTORY ${GENERATED_SOURCE_DIR})

set(GENERATED_SOURCES "")

foreach(proto_file ${proto_files})
  string(REPLACE ${proto_path} ${GENERATED_SOURCE_DIR} proto_file ${proto_file})
  string(REPLACE ".proto" ".pb.cc" proto_cpp_src ${proto_file})
  list(APPEND GENERATED_SOURCES ${proto_cpp_src})
  string(REPLACE ".proto" ".pb.h" proto_hdr_src ${proto_file})
  list(APPEND GENERATED_SOURCES ${proto_hdr_src})
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

add_library(${LIBRARY_NAME} ${SOURCES} ${GENERATED_SOURCES} ${HEADERS})
add_library(${PROJECT_NAME}::${LIBRARY_COMPONENT} ALIAS ${LIBRARY_NAME})

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
  $<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/lib/>
  $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/lib/>
  $<INSTALL_INTERFACE:$<INSTALL_PREFIX>/include/>
  PRIVATE
  $<BUILD_INTERFACE:${GENERATED_SOURCE_DIR}>/)

target_link_libraries(
  ${LIBRARY_NAME}
  PUBLIC
  Boost::boost
  Boost::filesystem
  nlohmann_json::nlohmann_json
  Threads::Threads
  ${PROJECT_NAME}::core
  ${PROJECT_NAME}::io
  PRIVATE
  Boost::url
  protobuf::libprotobuf)

# install the library

install(TARGETS ${LIBRARY_NAME}
  EXPORT ${CMAKE_PACKAGE_TARGETS}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

# install headers

install_headers("${HEADERS}" "${CMAKE_INSTALL_INCLUDEDIR}")

# Xcode source group

if(${CMAKE_GENERATOR} STREQUAL Xcode)
  get_target_property(TARGET_SOURCES ${LIBRARY_NAME} SOURCES)
  list(REMOVE_ITEM TARGET_SOURCES ${GENERATED_SOURCES})
  source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${TARGET_SOURCES})
endif()

# generate includes.hpp header file

set(INCLUDES_FILE "${CMAKE_CURRENT_BINARY_DIR}/includes.hpp")

add_custom_command(
  OUTPUT ${INCLUDES_FILE}
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  COMMAND ${CMAKE_COMMAND} -DOUTPUT_FILE=${INCLUDES_FILE} -DHEADERS_DIR=${CMAKE_CURRENT_SOURCE_DIR} -P ${PROJECT_SOURCE_DIR}/cmake/generate-includes-file.cmake
  COMMENT "Generating includes.hpp header file")

add_custom_target(GenerateIncludesFile${LIBRARY_NAME} DEPENDS ${INCLUDES_FILE})

add_dependencies(${LIBRARY_NAME} GenerateIncludesFile${LIBRARY_NAME})

install_headers_prefix("${INCLUDES_FILE}" "${CMAKE_INSTALL_INCLUDEDIR}" "${PROJECT_BINARY_DIR}/lib/")
