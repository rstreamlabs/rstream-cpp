# See LICENSE file in the project root for license information.

# executables

add_executable(${PROJECT_NAME}-example-io-metrics example_io_metrics.cpp)
target_link_libraries(${PROJECT_NAME}-example-io-metrics docopt::docopt_s_maybe ${PROJECT_NAME}::${PROJECT_NAME})
