# See LICENSE file in the project root for license information.

add_executable(${PROJECT_NAME}-nperf-server main.cpp)
target_link_libraries(${PROJECT_NAME}-nperf-server docopt::docopt_s_maybe ${PROJECT_NAME}::nperf ${PROJECT_NAME}::${PROJECT_NAME})
rstream_enable_runtime_plugins(${PROJECT_NAME}-nperf-server ${PROJECT_NAME}-plugin-io-generic ${PROJECT_NAME}-plugin-io-rstrm)
if(INSTALL_RPATH_KEYWORD)
  set_target_properties(${PROJECT_NAME}-nperf-server PROPERTIES ${INSTALL_RPATH_KEYWORD} ${RPATH_EXECUTABLE})
endif()
install(TARGETS ${PROJECT_NAME}-nperf-server DESTINATION ${CMAKE_INSTALL_BINDIR})
