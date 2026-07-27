# See LICENSE file in the project root for license information.

# enable testing

enable_testing()

# define macros for testing

set(BIN_OUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/test")

set(RSTREAM_TEST_TIMEOUT_SCALE "1" CACHE STRING "Timeout scale for instrumented tests")
set(RSTREAM_TEST_TIMEOUT_SECONDS "120" CACHE STRING "Maximum duration of one CTest test")

if(NOT RSTREAM_TEST_TIMEOUT_SCALE MATCHES "^[1-9][0-9]*$")
  message(FATAL_ERROR "RSTREAM_TEST_TIMEOUT_SCALE must be a positive integer.")
endif()

if(NOT RSTREAM_TEST_TIMEOUT_SECONDS MATCHES "^[1-9][0-9]*$")
  message(FATAL_ERROR "RSTREAM_TEST_TIMEOUT_SECONDS must be a positive integer.")
endif()

function(rstream_configure_test name)
  set_tests_properties(${name} PROPERTIES TIMEOUT ${RSTREAM_TEST_TIMEOUT_SECONDS})
endfunction()

macro(add_test_target name sources)
  add_executable(${name} ${sources})
  set_target_properties(${name} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${BIN_OUT_DIR})
  if(MSVC)
    target_compile_options(${name} PRIVATE /FI${PROJECT_SOURCE_DIR}/test/support/rstream/test/assert.hpp)
  else()
    target_compile_options(${name} PRIVATE -include${PROJECT_SOURCE_DIR}/test/support/rstream/test/assert.hpp)
  endif()
  target_compile_definitions(${name} PRIVATE RSTREAM_TEST_TIMEOUT_SCALE=${RSTREAM_TEST_TIMEOUT_SCALE})
  target_include_directories(${name} PRIVATE ${PROJECT_SOURCE_DIR}/test/support)
  target_link_libraries(${name} PRIVATE Boost::boost ${ARGN})
  add_test(NAME ${name} WORKING_DIRECTORY ${BIN_OUT_DIR} COMMAND ${name})
  rstream_configure_test(${name})
  set_property(GLOBAL APPEND PROPERTY RSTREAM_TEST_TARGETS ${name})
endmacro()

macro(add_check_target)
  set(multiValueArgs TESTS)
  cmake_parse_arguments(test_targets "" "" "${multiValueArgs}" ${ARGN})
  get_property(is_multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
  set(ctest_args --output-on-failure)
  if(is_multi_config)
    list(APPEND ctest_args -C $<CONFIG>)
  endif()
  get_property(rstream_test_targets GLOBAL PROPERTY RSTREAM_TEST_TARGETS)
  list(APPEND test_targets_TESTS ${rstream_test_targets})
  add_custom_target(check
    COMMAND ${CMAKE_COMMAND} -E env GTEST_COLOR=1 ${CMAKE_CTEST_COMMAND} ${ctest_args}
    DEPENDS ${test_targets_TESTS})
endmacro()
