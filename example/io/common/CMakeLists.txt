# See LICENSE file in the project root for license information.

# executables

if(WITH_MAXMINDDB)
  add_executable(${PROJECT_NAME}-example-io-geoip example_io_geoip.cpp)
  target_link_libraries(${PROJECT_NAME}-example-io-geoip docopt::docopt_s_maybe ${PROJECT_NAME}::${PROJECT_NAME})
endif()

add_executable(${PROJECT_NAME}-example-io-payloader-client example_io_payloader_client.cpp)
target_link_libraries(${PROJECT_NAME}-example-io-payloader-client ${PROJECT_NAME}::${PROJECT_NAME})

add_executable(${PROJECT_NAME}-example-io-payloader-server example_io_payloader_server.cpp)
target_link_libraries(${PROJECT_NAME}-example-io-payloader-server ${PROJECT_NAME}::${PROJECT_NAME})

add_executable(${PROJECT_NAME}-example-io-payloader example_io_payloader.cpp)
target_link_libraries(${PROJECT_NAME}-example-io-payloader ${PROJECT_NAME}::${PROJECT_NAME})

add_executable(${PROJECT_NAME}-example-io-queue-client example_io_queue_client.cpp)
target_link_libraries(${PROJECT_NAME}-example-io-queue-client ${PROJECT_NAME}::${PROJECT_NAME})
