# See LICENSE file in the project root for license information.

# process tests

add_test_target(${PROJECT_NAME}-test-io-common-payloader test_io_common_payloader.cpp ${PROJECT_NAME}::io)
add_test_target(${PROJECT_NAME}-test-io-common-payloader-limits test_io_common_payloader_limits.cpp ${PROJECT_NAME}::io)
add_test_target(${PROJECT_NAME}-test-io-common-queue test_io_common_queue.cpp ${PROJECT_NAME}::io)
add_test_target(${PROJECT_NAME}-test-io-common-address-url test_io_common_address_url.cpp ${PROJECT_NAME}::io)
add_test_target(${PROJECT_NAME}-test-io-common-geoip test_io_common_geoip.cpp ${PROJECT_NAME}::io Boost::filesystem)
add_test_target(${PROJECT_NAME}-test-io-common-stream-tcp test_io_common_stream_tcp.cpp ${PROJECT_NAME}::io ${PROJECT_NAME}::${PROJECT_NAME})
rstream_enable_runtime_plugins(${PROJECT_NAME}-test-io-common-stream-tcp ${PROJECT_NAME}-plugin-io-generic)
if(UNIX)
  add_test_target(${PROJECT_NAME}-test-io-common-stream-unix-serial test_io_common_stream_unix_serial.cpp ${PROJECT_NAME}::io ${PROJECT_NAME}::${PROJECT_NAME})
  rstream_enable_runtime_plugins(${PROJECT_NAME}-test-io-common-stream-unix-serial ${PROJECT_NAME}-plugin-io-generic)
endif()
add_test_target(${PROJECT_NAME}-test-io-common-stream-rstrm test_io_common_stream_rstrm.cpp ${PROJECT_NAME}::io ${PROJECT_NAME}::${PROJECT_NAME})
rstream_enable_runtime_plugins(${PROJECT_NAME}-test-io-common-stream-rstrm ${PROJECT_NAME}-plugin-io-generic ${PROJECT_NAME}-plugin-io-rstrm)
add_test_target(${PROJECT_NAME}-test-io-common-stream-tls test_io_common_stream_tls.cpp ${PROJECT_NAME}::io ${PROJECT_NAME}::${PROJECT_NAME})
rstream_enable_runtime_plugins(${PROJECT_NAME}-test-io-common-stream-tls ${PROJECT_NAME}-plugin-io-generic)
