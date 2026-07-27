# See LICENSE file in the project root for license information.

# process tests

set(NPERF_GENERATED_SOURCE_DIR "${PROJECT_BINARY_DIR}/bin/nperf/lib/cpp/rstream/nperf/generated-cpp-sources")

add_test_target(${PROJECT_NAME}-test-nperf-common test_nperf_common.cpp ${PROJECT_NAME}::nperf protobuf::libprotobuf)
target_include_directories(${PROJECT_NAME}-test-nperf-common SYSTEM PRIVATE ${NPERF_GENERATED_SOURCE_DIR})

add_test_target(${PROJECT_NAME}-test-nperf-runtime test_nperf_runtime.cpp ${PROJECT_NAME}::nperf ${PROJECT_NAME}::${PROJECT_NAME} protobuf::libprotobuf)
target_include_directories(${PROJECT_NAME}-test-nperf-runtime SYSTEM PRIVATE ${NPERF_GENERATED_SOURCE_DIR})
rstream_enable_runtime_plugins(${PROJECT_NAME}-test-nperf-runtime ${PROJECT_NAME}-plugin-io-generic)
