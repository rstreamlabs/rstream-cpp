# See LICENSE file in the project root for license information.

add_executable(${PROJECT_NAME}-ncat main.cpp)
target_link_libraries(${PROJECT_NAME}-ncat docopt::docopt_s_maybe ${PROJECT_NAME}::ncat ${PROJECT_NAME}::${PROJECT_NAME})
if(INSTALL_RPATH_KEYWORD)
  set_target_properties(${PROJECT_NAME}-ncat PROPERTIES ${INSTALL_RPATH_KEYWORD} ${RPATH_EXECUTABLE})
endif()
install(TARGETS ${PROJECT_NAME}-ncat DESTINATION ${CMAKE_INSTALL_BINDIR})
