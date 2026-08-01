# See LICENSE file in the project root for license information.

# process tests

add_test_target(${PROJECT_NAME}-test-core-metrics test_core_metrics.cpp ${PROJECT_NAME}::core)
