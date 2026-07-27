# See LICENSE file in the project root for license information.

cmake_minimum_required(VERSION 3.16)
project(test_package LANGUAGES CXX)
find_package(rstream REQUIRED CONFIG)

get_target_property(RSTREAM_COMPILE_FEATURES rstream::rstream INTERFACE_COMPILE_FEATURES)
if(NOT cxx_std_20 IN_LIST RSTREAM_COMPILE_FEATURES)
  message(FATAL_ERROR "rstream::rstream does not expose its C++20 requirement")
endif()

get_target_property(RSTREAM_CORE_TYPE rstream::core TYPE)
if(RSTREAM_TEST_EXPECT_SHARED_LIBS)
  if(NOT RSTREAM_CORE_TYPE STREQUAL "SHARED_LIBRARY")
    message(FATAL_ERROR "Expected shared rstream libraries, found ${RSTREAM_CORE_TYPE}")
  endif()
elseif(NOT RSTREAM_CORE_TYPE STREQUAL "STATIC_LIBRARY")
  message(FATAL_ERROR "Expected static rstream libraries, found ${RSTREAM_CORE_TYPE}")
endif()

get_target_property(RSTREAM_IO_GENERIC_TYPE rstream::plugin::io-generic TYPE)
get_target_property(RSTREAM_IO_RSTRM_TYPE rstream::plugin::io-rstrm TYPE)
if(RSTREAM_TEST_EXPECT_STATIC_PLUGINS)
  if(NOT RSTREAM_IO_GENERIC_TYPE STREQUAL "STATIC_LIBRARY" OR NOT RSTREAM_IO_RSTRM_TYPE STREQUAL "STATIC_LIBRARY")
    message(
      FATAL_ERROR
      "Expected static rstream plugins, found ${RSTREAM_IO_GENERIC_TYPE} and ${RSTREAM_IO_RSTRM_TYPE}")
  endif()
else()
  if(NOT RSTREAM_IO_GENERIC_TYPE STREQUAL "MODULE_LIBRARY" OR NOT RSTREAM_IO_RSTRM_TYPE STREQUAL "MODULE_LIBRARY")
    message(
      FATAL_ERROR
      "Expected dynamic rstream plugins, found ${RSTREAM_IO_GENERIC_TYPE} and ${RSTREAM_IO_RSTRM_TYPE}")
  endif()
endif()

add_executable(${PROJECT_NAME} main.cpp)
set_target_properties(
  ${PROJECT_NAME}
  PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}/bin")
target_link_libraries(${PROJECT_NAME} PUBLIC rstream::rstream)
rstream_enable_runtime_plugins(
  ${PROJECT_NAME}
  rstream::plugin::io-generic
  rstream::plugin::io-rstrm)
install(TARGETS ${PROJECT_NAME} DESTINATION ${CMAKE_INSTALL_BINDIR})
