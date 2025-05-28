# See LICENSE file in the project root for license information.

# executables

add_executable(${PROJECT_NAME}-example-core-allocator example_core_allocator.cpp)
target_link_libraries(${PROJECT_NAME}-example-core-allocator ${PROJECT_NAME}::core)

add_executable(${PROJECT_NAME}-example-core-buffer example_core_buffer.cpp)
target_link_libraries(${PROJECT_NAME}-example-core-buffer ${PROJECT_NAME}::core)

add_executable(${PROJECT_NAME}-example-core-exception example_core_exception.cpp)
target_link_libraries(${PROJECT_NAME}-example-core-exception ${PROJECT_NAME}::core)

add_executable(${PROJECT_NAME}-example-core-crc32 example_core_crc32.cpp)
target_link_libraries(${PROJECT_NAME}-example-core-crc32 ${PROJECT_NAME}::core)

add_executable(${PROJECT_NAME}-example-core-log example_core_log.cpp)
target_link_libraries(${PROJECT_NAME}-example-core-log ${PROJECT_NAME}::core)

add_executable(${PROJECT_NAME}-example-core-memory example_core_memory.cpp)
target_link_libraries(${PROJECT_NAME}-example-core-memory ${PROJECT_NAME}::core)
