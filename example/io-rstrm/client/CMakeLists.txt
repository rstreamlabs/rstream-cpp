# See LICENSE file in the project root for license information.

# executables

add_executable(${PROJECT_NAME}-example-io-rstrm-client example_io_rstrm_client.cpp)
target_link_libraries(${PROJECT_NAME}-example-io-rstrm-client docopt::docopt_s_maybe ${PROJECT_NAME}::${PROJECT_NAME})
