# See LICENSE file in the project root for license information.

# enable testing

enable_testing()

# define macros for testing

set(BIN_OUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/test")

macro(add_test_target name sources)
  add_executable(${name} ${sources})
  set_target_properties(${name} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${BIN_OUT_DIR})
  target_link_libraries(${name} Boost::boost GTest::GTest ${PROJECT_NAME}::${PROJECT_NAME})
  add_test(NAME ${name} WORKING_DIRECTORY ${BIN_OUT_DIR} COMMAND ${name})
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
