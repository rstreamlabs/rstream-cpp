# See LICENSE file in the project root for license information.

# process tests

set(NPERF_GENERATED_SOURCE_DIR "${PROJECT_BINARY_DIR}/bin/nperf/lib/cpp/rstream/nperf/generated-cpp-sources")

add_test_target(${PROJECT_NAME}-test-nperf-common test_nperf_common.cpp)
target_include_directories(${PROJECT_NAME}-test-nperf-common PRIVATE ${NPERF_GENERATED_SOURCE_DIR})
target_link_libraries(${PROJECT_NAME}-test-nperf-common ${PROJECT_NAME}::nperf protobuf::libprotobuf)

add_test_target(${PROJECT_NAME}-test-nperf-runtime test_nperf_runtime.cpp)
target_include_directories(${PROJECT_NAME}-test-nperf-runtime PRIVATE ${NPERF_GENERATED_SOURCE_DIR})
target_link_libraries(${PROJECT_NAME}-test-nperf-runtime ${PROJECT_NAME}::nperf protobuf::libprotobuf)
