# See LICENSE file in the project root for license information.

# process tests

add_test_target(${PROJECT_NAME}-test-tunnel-status test_tunnel_status.cpp ${PROJECT_NAME}::tunnel)
add_test_target(${PROJECT_NAME}-test-tunnel-proxy test_tunnel_proxy.cpp ${PROJECT_NAME}::tunnel ${PROJECT_NAME}::io-rstrm-protobuf ${PROJECT_NAME}::${PROJECT_NAME})
target_include_directories(${PROJECT_NAME}-test-tunnel-proxy PRIVATE ${PROJECT_BINARY_DIR}/lib/rstream/io-rstrm/generated-cpp-sources)
rstream_enable_runtime_plugins(${PROJECT_NAME}-test-tunnel-proxy ${PROJECT_NAME}-plugin-io-generic ${PROJECT_NAME}-plugin-io-rstrm)
