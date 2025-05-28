# See LICENSE file in the project root for license information.

# executables

add_executable(${PROJECT_NAME}-example-io-http-client example_io_http_client.cpp)
target_link_libraries(${PROJECT_NAME}-example-io-http-client docopt::docopt_s_maybe ${PROJECT_NAME}::${PROJECT_NAME})

add_executable(${PROJECT_NAME}-example-io-http-server example_io_http_server.cpp)
target_link_libraries(${PROJECT_NAME}-example-io-http-server docopt::docopt_s_maybe ${PROJECT_NAME}::${PROJECT_NAME})

add_executable(${PROJECT_NAME}-example-io-http-upgrade example_io_http_upgrade.cpp)
target_link_libraries(${PROJECT_NAME}-example-io-http-upgrade docopt::docopt_s_maybe ${PROJECT_NAME}::${PROJECT_NAME})
