# See LICENSE file in the project root for license information.

add_subdirectory(client)

foreach(example IN ITEMS http-server beast-tunnel tls-http-server websocket-server private-echo callback future coroutine)
  add_executable(${PROJECT_NAME}-example-${example} ${example}.cpp)
  target_link_libraries(${PROJECT_NAME}-example-${example} PRIVATE ${PROJECT_NAME}::${PROJECT_NAME})
  rstream_enable_runtime_plugins(${PROJECT_NAME}-example-${example} ${PROJECT_NAME}-plugin-io-generic ${PROJECT_NAME}-plugin-io-rstrm)
endforeach()

add_executable(${PROJECT_NAME}-example-uri-http-server ../io/http/example_io_http_server.cpp)
target_link_libraries(${PROJECT_NAME}-example-uri-http-server PRIVATE docopt::docopt_s_maybe ${PROJECT_NAME}::${PROJECT_NAME})
rstream_enable_runtime_plugins(${PROJECT_NAME}-example-uri-http-server ${PROJECT_NAME}-plugin-io-generic ${PROJECT_NAME}-plugin-io-rstrm)

# This entry point is ordinary Boost.Asio/Beast: it has no rstream dependency.
add_executable(${PROJECT_NAME}-example-beast-local beast-local.cpp)
target_compile_features(${PROJECT_NAME}-example-beast-local PRIVATE cxx_std_20)
target_link_libraries(${PROJECT_NAME}-example-beast-local PRIVATE Boost::boost Threads::Threads)

if(ENABLE_TESTING)
  find_package(Python3 COMPONENTS Interpreter REQUIRED)
  add_test(NAME ${PROJECT_NAME}-test-io-rstrm-examples
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_programs.py
      $<TARGET_FILE:${PROJECT_NAME}-example-callback>
      $<TARGET_FILE:${PROJECT_NAME}-example-future>
      $<TARGET_FILE:${PROJECT_NAME}-example-coroutine>
      $<TARGET_FILE:${PROJECT_NAME}-example-private-echo>
      $<TARGET_FILE:${PROJECT_NAME}-example-http-server>
      $<TARGET_FILE:${PROJECT_NAME}-example-websocket-server>
      $<TARGET_FILE:${PROJECT_NAME}-example-beast-tunnel>
      $<TARGET_FILE:${PROJECT_NAME}-example-beast-local>)
  rstream_configure_test(${PROJECT_NAME}-test-io-rstrm-examples)
  set_property(GLOBAL APPEND PROPERTY RSTREAM_TEST_TARGETS
    ${PROJECT_NAME}-example-callback ${PROJECT_NAME}-example-future
    ${PROJECT_NAME}-example-coroutine
    ${PROJECT_NAME}-example-private-echo ${PROJECT_NAME}-example-http-server
    ${PROJECT_NAME}-example-websocket-server ${PROJECT_NAME}-example-beast-tunnel
    ${PROJECT_NAME}-example-beast-local)
endif()
