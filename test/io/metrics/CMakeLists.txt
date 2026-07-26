# See LICENSE file in the project root for license information.

# process tests

add_test_target(${PROJECT_NAME}-test-io-metrics-exposer test_io_metrics_exposer.cpp ${PROJECT_NAME}::io ${PROJECT_NAME}::${PROJECT_NAME})
rstream_enable_runtime_plugins(${PROJECT_NAME}-test-io-metrics-exposer ${PROJECT_NAME}-plugin-io-generic)
