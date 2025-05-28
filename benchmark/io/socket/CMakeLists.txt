# See LICENSE file in the project root for license information.

# executables

add_executable(${PROJECT_NAME}-benchmark-io-socket-client client.cpp)
target_link_libraries(${PROJECT_NAME}-benchmark-io-socket-client docopt::docopt_s_maybe ${PROJECT_NAME}::${PROJECT_NAME})
if(INSTALL_RPATH_KEYWORD)
  set_target_properties(${PROJECT_NAME}-benchmark-io-socket-client PROPERTIES ${INSTALL_RPATH_KEYWORD} ${RPATH_EXECUTABLE})
endif()

add_executable(${PROJECT_NAME}-benchmark-io-socket-server server.cpp)
target_link_libraries(${PROJECT_NAME}-benchmark-io-socket-server docopt::docopt_s_maybe ${PROJECT_NAME}::${PROJECT_NAME})
if(INSTALL_RPATH_KEYWORD)
  set_target_properties(${PROJECT_NAME}-benchmark-io-socket-server PROPERTIES ${INSTALL_RPATH_KEYWORD} ${RPATH_EXECUTABLE})
endif()
