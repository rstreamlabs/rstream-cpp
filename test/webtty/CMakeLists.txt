# See LICENSE file in the project root for license information.

# process tests

set(WEBTTY_GENERATED_SOURCE_DIR "${PROJECT_BINARY_DIR}/bin/webtty/lib/rstream/webtty/generated-cpp-sources")

add_test_target(${PROJECT_NAME}-test-webtty-protocol test_webtty_protocol.cpp ${PROJECT_NAME}::webtty ${PROJECT_NAME}::io-rstrm Boost::url protobuf::libprotobuf)
target_include_directories(${PROJECT_NAME}-test-webtty-protocol SYSTEM PRIVATE ${WEBTTY_GENERATED_SOURCE_DIR})

add_test_target(${PROJECT_NAME}-test-webtty-convert test_webtty_convert.cpp ${PROJECT_NAME}::webtty protobuf::libprotobuf)
target_include_directories(${PROJECT_NAME}-test-webtty-convert SYSTEM PRIVATE ${WEBTTY_GENERATED_SOURCE_DIR})

add_test_target(${PROJECT_NAME}-test-webtty-payload-crypto test_webtty_payload_crypto.cpp ${PROJECT_NAME}::webtty)

add_test_target(${PROJECT_NAME}-test-webtty-auth-proof test_webtty_auth_proof.cpp ${PROJECT_NAME}::webtty)

add_test_target(${PROJECT_NAME}-test-webtty-cli-config test_webtty_cli_config.cpp ${PROJECT_NAME}::webtty nlohmann_json::nlohmann_json yaml-cpp::yaml-cpp)

add_test_target(${PROJECT_NAME}-test-webtty-terminal-stream test_webtty_terminal_stream.cpp ${PROJECT_NAME}::webtty)

if(UNIX)
  add_test_target(${PROJECT_NAME}-test-webtty-plain-server-runtime test_webtty_plain_server_runtime.cpp ${PROJECT_NAME}::webtty ${PROJECT_NAME}::${PROJECT_NAME} protobuf::libprotobuf)
  target_include_directories(${PROJECT_NAME}-test-webtty-plain-server-runtime SYSTEM PRIVATE ${WEBTTY_GENERATED_SOURCE_DIR})
  rstream_enable_runtime_plugins(${PROJECT_NAME}-test-webtty-plain-server-runtime ${PROJECT_NAME}-plugin-io-generic)

  add_test_target(${PROJECT_NAME}-test-webtty-websocket-server-runtime test_webtty_websocket_server_runtime.cpp ${PROJECT_NAME}::webtty ${PROJECT_NAME}::${PROJECT_NAME} protobuf::libprotobuf)
  target_include_directories(${PROJECT_NAME}-test-webtty-websocket-server-runtime SYSTEM PRIVATE ${WEBTTY_GENERATED_SOURCE_DIR})
  rstream_enable_runtime_plugins(${PROJECT_NAME}-test-webtty-websocket-server-runtime ${PROJECT_NAME}-plugin-io-generic)

  add_test_target(${PROJECT_NAME}-test-webtty-plain-client-runtime test_webtty_plain_client_runtime.cpp ${PROJECT_NAME}::webtty ${PROJECT_NAME}::${PROJECT_NAME} protobuf::libprotobuf)
  target_include_directories(${PROJECT_NAME}-test-webtty-plain-client-runtime SYSTEM PRIVATE ${WEBTTY_GENERATED_SOURCE_DIR})
  rstream_enable_runtime_plugins(${PROJECT_NAME}-test-webtty-plain-client-runtime ${PROJECT_NAME}-plugin-io-generic)

  add_test_target(${PROJECT_NAME}-test-webtty-websocket-client-runtime test_webtty_websocket_client_runtime.cpp ${PROJECT_NAME}::webtty ${PROJECT_NAME}::${PROJECT_NAME} protobuf::libprotobuf)
  target_include_directories(${PROJECT_NAME}-test-webtty-websocket-client-runtime SYSTEM PRIVATE ${WEBTTY_GENERATED_SOURCE_DIR})
  rstream_enable_runtime_plugins(${PROJECT_NAME}-test-webtty-websocket-client-runtime ${PROJECT_NAME}-plugin-io-generic)
endif()
