# See LICENSE file in the project root for license information.

# process tests

add_test_target(${PROJECT_NAME}-test-tunnel-status test_tunnel_status.cpp)
target_link_libraries(${PROJECT_NAME}-test-tunnel-status ${PROJECT_NAME}::tunnel)
add_test_target(${PROJECT_NAME}-test-tunnel-proxy test_tunnel_proxy.cpp)
target_link_libraries(${PROJECT_NAME}-test-tunnel-proxy ${PROJECT_NAME}::tunnel)
target_include_directories(${PROJECT_NAME}-test-tunnel-proxy PRIVATE ${PROJECT_BINARY_DIR}/lib/rstream/io-rstrm/generated-cpp-sources)
add_dependencies(${PROJECT_NAME}-test-tunnel-proxy ${PROJECT_NAME}-plugin-io-generic ${PROJECT_NAME}-plugin-io-rstrm)
add_custom_command(TARGET ${PROJECT_NAME}-test-tunnel-proxy POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${PROJECT_NAME}-test-tunnel-proxy>/../lib/rstream"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:${PROJECT_NAME}-plugin-io-generic>" "$<TARGET_FILE_DIR:${PROJECT_NAME}-test-tunnel-proxy>/../lib/rstream/"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:${PROJECT_NAME}-plugin-io-rstrm>" "$<TARGET_FILE_DIR:${PROJECT_NAME}-test-tunnel-proxy>/../lib/rstream/")
