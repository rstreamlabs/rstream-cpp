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
  list(APPEND test_targets ${targets} PARENT_SCOPE)
endmacro()

macro(add_check_target)
  set(multiValueArgs TESTS)
  cmake_parse_arguments(test_targets "" "" "${multiValueArgs}" ${ARGN})
  add_custom_target(check
    COMMAND GTEST_COLOR=1 ${CMAKE_CTEST_COMMAND} -C Debug
    DEPENDS ${test_targets_TESTS})
endmacro()
