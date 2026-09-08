# See LICENSE file in the project root for license information.

# executables

add_executable(${PROJECT_NAME}-example-io-http-client example_io_http_client.cpp)
target_link_libraries(${PROJECT_NAME}-example-io-http-client docopt::docopt_s_maybe ${PROJECT_NAME}::${PROJECT_NAME})
rstream_enable_runtime_plugins(${PROJECT_NAME}-example-io-http-client ${PROJECT_NAME}-plugin-io-generic ${PROJECT_NAME}-plugin-io-rstrm)

add_executable(${PROJECT_NAME}-example-io-http-server example_io_http_server.cpp)
target_link_libraries(${PROJECT_NAME}-example-io-http-server docopt::docopt_s_maybe ${PROJECT_NAME}::${PROJECT_NAME})
rstream_enable_runtime_plugins(${PROJECT_NAME}-example-io-http-server ${PROJECT_NAME}-plugin-io-generic ${PROJECT_NAME}-plugin-io-rstrm)

add_executable(${PROJECT_NAME}-example-io-http-upgrade example_io_http_upgrade.cpp)
target_link_libraries(${PROJECT_NAME}-example-io-http-upgrade docopt::docopt_s_maybe ${PROJECT_NAME}::${PROJECT_NAME})
rstream_enable_runtime_plugins(${PROJECT_NAME}-example-io-http-upgrade ${PROJECT_NAME}-plugin-io-generic ${PROJECT_NAME}-plugin-io-rstrm)

if(ENABLE_TESTING)
  find_package(Python3 COMPONENTS Interpreter REQUIRED)
  add_test(NAME ${PROJECT_NAME}-test-io-http-examples
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_http_examples.py
      $<TARGET_FILE:${PROJECT_NAME}-example-io-http-server>
      $<TARGET_FILE:${PROJECT_NAME}-example-io-http-client>)
  rstream_configure_test(${PROJECT_NAME}-test-io-http-examples)
  set_property(GLOBAL APPEND PROPERTY RSTREAM_TEST_TARGETS
    ${PROJECT_NAME}-example-io-http-server ${PROJECT_NAME}-example-io-http-client)
endif()
