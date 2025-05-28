# See LICENSE file in the project root for license information.

# executables

add_executable(${PROJECT_NAME}-example-core-metrics example_core_metrics.cpp)
target_link_libraries(${PROJECT_NAME}-example-core-metrics docopt::docopt_s_maybe ${PROJECT_NAME}::core)
