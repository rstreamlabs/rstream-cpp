# See LICENSE file in the project root for license information.

# process tests

set(WEBTTY_GENERATED_SOURCE_DIR "${PROJECT_BINARY_DIR}/bin/webtty/lib/rstream/webtty/generated-cpp-sources")

add_test_target(${PROJECT_NAME}-test-webtty-protocol test_webtty_protocol.cpp)
target_include_directories(${PROJECT_NAME}-test-webtty-protocol PRIVATE ${WEBTTY_GENERATED_SOURCE_DIR})
target_link_libraries(${PROJECT_NAME}-test-webtty-protocol ${PROJECT_NAME}::webtty Boost::url protobuf::libprotobuf)

add_test_target(${PROJECT_NAME}-test-webtty-convert test_webtty_convert.cpp)
target_include_directories(${PROJECT_NAME}-test-webtty-convert PRIVATE ${WEBTTY_GENERATED_SOURCE_DIR})
target_link_libraries(${PROJECT_NAME}-test-webtty-convert ${PROJECT_NAME}::webtty protobuf::libprotobuf)

add_test_target(${PROJECT_NAME}-test-webtty-payload-crypto test_webtty_payload_crypto.cpp)
target_link_libraries(${PROJECT_NAME}-test-webtty-payload-crypto ${PROJECT_NAME}::webtty)

add_test_target(${PROJECT_NAME}-test-webtty-auth-proof test_webtty_auth_proof.cpp)
target_link_libraries(${PROJECT_NAME}-test-webtty-auth-proof ${PROJECT_NAME}::webtty)

add_test_target(${PROJECT_NAME}-test-webtty-cli-config test_webtty_cli_config.cpp)
target_link_libraries(${PROJECT_NAME}-test-webtty-cli-config ${PROJECT_NAME}::webtty nlohmann_json::nlohmann_json yaml-cpp::yaml-cpp)

add_test_target(${PROJECT_NAME}-test-webtty-terminal-stream test_webtty_terminal_stream.cpp)
target_link_libraries(${PROJECT_NAME}-test-webtty-terminal-stream ${PROJECT_NAME}::webtty)

add_test_target(${PROJECT_NAME}-test-webtty-plain-server-runtime test_webtty_plain_server_runtime.cpp)
target_include_directories(${PROJECT_NAME}-test-webtty-plain-server-runtime PRIVATE ${WEBTTY_GENERATED_SOURCE_DIR})
target_link_libraries(${PROJECT_NAME}-test-webtty-plain-server-runtime ${PROJECT_NAME}::webtty protobuf::libprotobuf)
add_dependencies(${PROJECT_NAME}-test-webtty-plain-server-runtime ${PROJECT_NAME}-plugin-io-generic)
add_custom_command(TARGET ${PROJECT_NAME}-test-webtty-plain-server-runtime POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${PROJECT_NAME}-test-webtty-plain-server-runtime>/../lib/rstream"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:${PROJECT_NAME}-plugin-io-generic>" "$<TARGET_FILE_DIR:${PROJECT_NAME}-test-webtty-plain-server-runtime>/../lib/rstream/")

add_test_target(${PROJECT_NAME}-test-webtty-websocket-server-runtime test_webtty_websocket_server_runtime.cpp)
target_include_directories(${PROJECT_NAME}-test-webtty-websocket-server-runtime PRIVATE ${WEBTTY_GENERATED_SOURCE_DIR})
target_link_libraries(${PROJECT_NAME}-test-webtty-websocket-server-runtime ${PROJECT_NAME}::webtty protobuf::libprotobuf)
add_dependencies(${PROJECT_NAME}-test-webtty-websocket-server-runtime ${PROJECT_NAME}-plugin-io-generic)
add_custom_command(TARGET ${PROJECT_NAME}-test-webtty-websocket-server-runtime POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${PROJECT_NAME}-test-webtty-websocket-server-runtime>/../lib/rstream"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:${PROJECT_NAME}-plugin-io-generic>" "$<TARGET_FILE_DIR:${PROJECT_NAME}-test-webtty-websocket-server-runtime>/../lib/rstream/")

add_test_target(${PROJECT_NAME}-test-webtty-plain-client-runtime test_webtty_plain_client_runtime.cpp)
target_include_directories(${PROJECT_NAME}-test-webtty-plain-client-runtime PRIVATE ${WEBTTY_GENERATED_SOURCE_DIR})
target_link_libraries(${PROJECT_NAME}-test-webtty-plain-client-runtime ${PROJECT_NAME}::webtty protobuf::libprotobuf)
add_dependencies(${PROJECT_NAME}-test-webtty-plain-client-runtime ${PROJECT_NAME}-plugin-io-generic)
add_custom_command(TARGET ${PROJECT_NAME}-test-webtty-plain-client-runtime POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${PROJECT_NAME}-test-webtty-plain-client-runtime>/../lib/rstream"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:${PROJECT_NAME}-plugin-io-generic>" "$<TARGET_FILE_DIR:${PROJECT_NAME}-test-webtty-plain-client-runtime>/../lib/rstream/")

add_test_target(${PROJECT_NAME}-test-webtty-websocket-client-runtime test_webtty_websocket_client_runtime.cpp)
target_include_directories(${PROJECT_NAME}-test-webtty-websocket-client-runtime PRIVATE ${WEBTTY_GENERATED_SOURCE_DIR})
target_link_libraries(${PROJECT_NAME}-test-webtty-websocket-client-runtime ${PROJECT_NAME}::webtty protobuf::libprotobuf)
add_dependencies(${PROJECT_NAME}-test-webtty-websocket-client-runtime ${PROJECT_NAME}-plugin-io-generic)
add_custom_command(TARGET ${PROJECT_NAME}-test-webtty-websocket-client-runtime POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${PROJECT_NAME}-test-webtty-websocket-client-runtime>/../lib/rstream"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:${PROJECT_NAME}-plugin-io-generic>" "$<TARGET_FILE_DIR:${PROJECT_NAME}-test-webtty-websocket-client-runtime>/../lib/rstream/")
