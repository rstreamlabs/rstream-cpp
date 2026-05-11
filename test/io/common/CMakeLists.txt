# See LICENSE file in the project root for license information.

# process tests

add_test_target(${PROJECT_NAME}-test-io-common-payloader test_io_common_payloader.cpp)
add_test_target(${PROJECT_NAME}-test-io-common-payloader-limits test_io_common_payloader_limits.cpp)
add_test_target(${PROJECT_NAME}-test-io-common-address-url test_io_common_address_url.cpp)
add_test_target(${PROJECT_NAME}-test-io-common-geoip test_io_common_geoip.cpp)
add_test_target(${PROJECT_NAME}-test-io-common-stream-tcp test_io_common_stream_tcp.cpp)
add_dependencies(${PROJECT_NAME}-test-io-common-stream-tcp ${PROJECT_NAME}-plugin-io-generic)
add_custom_command(TARGET ${PROJECT_NAME}-test-io-common-stream-tcp POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${PROJECT_NAME}-test-io-common-stream-tcp>/../lib/rstream"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:${PROJECT_NAME}-plugin-io-generic>" "$<TARGET_FILE_DIR:${PROJECT_NAME}-test-io-common-stream-tcp>/../lib/rstream/")
add_test_target(${PROJECT_NAME}-test-io-common-stream-unix-serial test_io_common_stream_unix_serial.cpp)
add_dependencies(${PROJECT_NAME}-test-io-common-stream-unix-serial ${PROJECT_NAME}-plugin-io-generic)
add_custom_command(TARGET ${PROJECT_NAME}-test-io-common-stream-unix-serial POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${PROJECT_NAME}-test-io-common-stream-unix-serial>/../lib/rstream"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:${PROJECT_NAME}-plugin-io-generic>" "$<TARGET_FILE_DIR:${PROJECT_NAME}-test-io-common-stream-unix-serial>/../lib/rstream/")
add_test_target(${PROJECT_NAME}-test-io-common-stream-rstrm test_io_common_stream_rstrm.cpp)
add_dependencies(${PROJECT_NAME}-test-io-common-stream-rstrm ${PROJECT_NAME}-plugin-io-generic ${PROJECT_NAME}-plugin-io-rstrm)
add_custom_command(TARGET ${PROJECT_NAME}-test-io-common-stream-rstrm POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${PROJECT_NAME}-test-io-common-stream-rstrm>/../lib/rstream"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:${PROJECT_NAME}-plugin-io-generic>" "$<TARGET_FILE_DIR:${PROJECT_NAME}-test-io-common-stream-rstrm>/../lib/rstream/"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:${PROJECT_NAME}-plugin-io-rstrm>" "$<TARGET_FILE_DIR:${PROJECT_NAME}-test-io-common-stream-rstrm>/../lib/rstream/")
add_test_target(${PROJECT_NAME}-test-io-common-stream-tls test_io_common_stream_tls.cpp)
add_dependencies(${PROJECT_NAME}-test-io-common-stream-tls ${PROJECT_NAME}-plugin-io-generic)
add_custom_command(TARGET ${PROJECT_NAME}-test-io-common-stream-tls POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${PROJECT_NAME}-test-io-common-stream-tls>/../lib/rstream"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:${PROJECT_NAME}-plugin-io-generic>" "$<TARGET_FILE_DIR:${PROJECT_NAME}-test-io-common-stream-tls>/../lib/rstream/")
