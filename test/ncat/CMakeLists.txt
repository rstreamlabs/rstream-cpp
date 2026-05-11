# See LICENSE file in the project root for license information.

# process tests

add_test_target(${PROJECT_NAME}-test-ncat-runtime test_ncat_runtime.cpp)
target_link_libraries(${PROJECT_NAME}-test-ncat-runtime ${PROJECT_NAME}::ncat)
add_dependencies(${PROJECT_NAME}-test-ncat-runtime ${PROJECT_NAME}-plugin-io-generic)
add_custom_command(TARGET ${PROJECT_NAME}-test-ncat-runtime POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${PROJECT_NAME}-test-ncat-runtime>/../lib/rstream"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:${PROJECT_NAME}-plugin-io-generic>" "$<TARGET_FILE_DIR:${PROJECT_NAME}-test-ncat-runtime>/../lib/rstream/")
