# See LICENSE file in the project root for license information.

# process tests

add_test_target(${PROJECT_NAME}-test-io-rstrm-convert test_io_rstrm_convert.cpp ${PROJECT_NAME}::io-rstrm ${PROJECT_NAME}::io-rstrm-protobuf)
target_include_directories(${PROJECT_NAME}-test-io-rstrm-convert PRIVATE ${PROJECT_BINARY_DIR}/lib/rstream/io-rstrm/generated-cpp-sources)
add_test_target(${PROJECT_NAME}-test-io-rstrm-config test_io_rstrm_config.cpp ${PROJECT_NAME}::io-rstrm Boost::url)
add_test_target(${PROJECT_NAME}-test-io-rstrm-handshake test_io_rstrm_handshake.cpp ${PROJECT_NAME}::io-rstrm ${PROJECT_NAME}::io-rstrm-protobuf)
target_include_directories(${PROJECT_NAME}-test-io-rstrm-handshake PRIVATE ${PROJECT_BINARY_DIR}/lib/rstream/io-rstrm/generated-cpp-sources)
add_test_target(${PROJECT_NAME}-test-io-rstrm-control-channel test_io_rstrm_control_channel.cpp ${PROJECT_NAME}::io-rstrm ${PROJECT_NAME}::io-rstrm-protobuf ${PROJECT_NAME}::${PROJECT_NAME})
target_include_directories(${PROJECT_NAME}-test-io-rstrm-control-channel PRIVATE ${PROJECT_BINARY_DIR}/lib/rstream/io-rstrm/generated-cpp-sources)
rstream_enable_runtime_plugins(${PROJECT_NAME}-test-io-rstrm-control-channel ${PROJECT_NAME}-plugin-io-generic ${PROJECT_NAME}-plugin-io-rstrm)
add_test_target(${PROJECT_NAME}-test-io-rstrm-endpoint test_io_rstrm_endpoint.cpp ${PROJECT_NAME}::io-rstrm Boost::url)
add_test_target(${PROJECT_NAME}-test-io-rstrm-format test_io_rstrm_format.cpp ${PROJECT_NAME}::io-rstrm)
