# See LICENSE file in the project root for license information.

# process tests

if(WIN32)
  find_program(RSTREAM_POWERSHELL_EXECUTABLE NAMES pwsh powershell)
  if(NOT RSTREAM_POWERSHELL_EXECUTABLE)
    message(FATAL_ERROR "PowerShell is required to run the Windows ncat runtime test.")
  endif()
  add_test(
    NAME ${PROJECT_NAME}-test-ncat-windows-runtime
    COMMAND
      ${RSTREAM_POWERSHELL_EXECUTABLE}
      -NoProfile
      -ExecutionPolicy Bypass
      -File ${CMAKE_CURRENT_SOURCE_DIR}/test_ncat_windows_runtime.ps1
      -Ncat $<TARGET_FILE:${PROJECT_NAME}-ncat>)
  rstream_configure_test(${PROJECT_NAME}-test-ncat-windows-runtime)
  set_property(GLOBAL APPEND PROPERTY RSTREAM_TEST_TARGETS ${PROJECT_NAME}-ncat)
else()
  add_test_target(${PROJECT_NAME}-test-ncat-runtime test_ncat_runtime.cpp ${PROJECT_NAME}::ncat ${PROJECT_NAME}::${PROJECT_NAME})
  rstream_enable_runtime_plugins(${PROJECT_NAME}-test-ncat-runtime ${PROJECT_NAME}-plugin-io-generic)
  add_test(
    NAME ${PROJECT_NAME}-test-ncat-cli-runtime
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_ncat_cli_runtime.py $<TARGET_FILE:${PROJECT_NAME}-ncat>)
  rstream_configure_test(${PROJECT_NAME}-test-ncat-cli-runtime)
  set_property(GLOBAL APPEND PROPERTY RSTREAM_TEST_TARGETS ${PROJECT_NAME}-ncat)
endif()
