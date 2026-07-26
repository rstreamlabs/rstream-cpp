# See LICENSE file in the project root for license information.

# process tests

add_subdirectory(core)
add_subdirectory(io)
add_subdirectory(ncat)
add_subdirectory(nperf)
add_subdirectory(tunnel)
add_subdirectory(webtty)

if(TARGET ${PROJECT_NAME}-inspect)
  add_test(
    NAME rstream-test-inspect-runtime-plugins
    COMMAND $<TARGET_FILE:${PROJECT_NAME}-inspect> plugins)
  set_tests_properties(
    rstream-test-inspect-runtime-plugins
    PROPERTIES
      PASS_REGULAR_EXPRESSION "name[ ]*: io-generic.*name[ ]*: io-rstrm")
  rstream_configure_test(rstream-test-inspect-runtime-plugins)
  set_property(GLOBAL APPEND PROPERTY RSTREAM_TEST_TARGETS ${PROJECT_NAME}-inspect)
endif()

if(Python3_Interpreter_FOUND)
  add_test(
    NAME rstream-test-ca-generator
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_embed_default_ca_certificates.py)
  rstream_configure_test(rstream-test-ca-generator)
  add_test(
    NAME rstream-test-deploy
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_deploy.py)
  rstream_configure_test(rstream-test-deploy)
  add_test(
    NAME rstream-test-toolchain-environment
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/test_toolchain_environment.py)
  rstream_configure_test(rstream-test-toolchain-environment)
endif()
