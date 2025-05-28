# See LICENSE file in the project root for license information.

add_executable(${PROJECT_NAME}-nperf-client main.cpp)
target_link_libraries(${PROJECT_NAME}-nperf-client docopt::docopt_s_maybe ${PROJECT_NAME}::nperf ${PROJECT_NAME}::${PROJECT_NAME})
if(INSTALL_RPATH_KEYWORD)
  set_target_properties(${PROJECT_NAME}-nperf-client PROPERTIES ${INSTALL_RPATH_KEYWORD} ${RPATH_EXECUTABLE})
endif()
install(TARGETS ${PROJECT_NAME}-nperf-client DESTINATION ${CMAKE_INSTALL_BINDIR})
