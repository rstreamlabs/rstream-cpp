# See LICENSE file in the project root for license information.

# process tests

add_test_target(${PROJECT_NAME}-test-core-executor-binder test_core_executor_binder.cpp)
add_test_target(${PROJECT_NAME}-test-core-memory-buffer test_core_memory_buffer.cpp)
add_test_target(${PROJECT_NAME}-test-core-exception-system test_core_exception_system.cpp)
add_test_target(${PROJECT_NAME}-test-core-plugin-version test_core_plugin_version.cpp)
add_test_target(${PROJECT_NAME}-test-core-random test_core_random.cpp)
