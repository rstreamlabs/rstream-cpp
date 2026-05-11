# See LICENSE file in the project root for license information.

# process tests

add_test_target(${PROJECT_NAME}-test-io-rstrm-convert test_io_rstrm_convert.cpp)
target_include_directories(${PROJECT_NAME}-test-io-rstrm-convert PRIVATE ${PROJECT_BINARY_DIR}/lib/rstream/io-rstrm/generated-cpp-sources)
add_test_target(${PROJECT_NAME}-test-io-rstrm-config test_io_rstrm_config.cpp)
target_link_libraries(${PROJECT_NAME}-test-io-rstrm-config Boost::url)
add_test_target(${PROJECT_NAME}-test-io-rstrm-handshake test_io_rstrm_handshake.cpp)
target_include_directories(${PROJECT_NAME}-test-io-rstrm-handshake PRIVATE ${PROJECT_BINARY_DIR}/lib/rstream/io-rstrm/generated-cpp-sources)
add_test_target(${PROJECT_NAME}-test-io-rstrm-control-channel test_io_rstrm_control_channel.cpp)
target_include_directories(${PROJECT_NAME}-test-io-rstrm-control-channel PRIVATE ${PROJECT_BINARY_DIR}/lib/rstream/io-rstrm/generated-cpp-sources)
add_dependencies(${PROJECT_NAME}-test-io-rstrm-control-channel ${PROJECT_NAME}-plugin-io-generic ${PROJECT_NAME}-plugin-io-rstrm)
add_custom_command(TARGET ${PROJECT_NAME}-test-io-rstrm-control-channel POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${PROJECT_NAME}-test-io-rstrm-control-channel>/../lib/rstream"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:${PROJECT_NAME}-plugin-io-generic>" "$<TARGET_FILE_DIR:${PROJECT_NAME}-test-io-rstrm-control-channel>/../lib/rstream/"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:${PROJECT_NAME}-plugin-io-rstrm>" "$<TARGET_FILE_DIR:${PROJECT_NAME}-test-io-rstrm-control-channel>/../lib/rstream/")
add_test_target(${PROJECT_NAME}-test-io-rstrm-endpoint test_io_rstrm_endpoint.cpp)
target_link_libraries(${PROJECT_NAME}-test-io-rstrm-endpoint Boost::url)
add_test_target(${PROJECT_NAME}-test-io-rstrm-format test_io_rstrm_format.cpp)
