# See LICENSE file in the project root for license information.

add_executable(${PROJECT_NAME}-file-server main.cpp)
target_link_libraries(${PROJECT_NAME}-file-server docopt::docopt_s_maybe ${PROJECT_NAME}::file-server ${PROJECT_NAME}::${PROJECT_NAME})
if(INSTALL_RPATH_KEYWORD)
  set_target_properties(${PROJECT_NAME}-file-server PROPERTIES ${INSTALL_RPATH_KEYWORD} ${RPATH_EXECUTABLE})
endif()
install(TARGETS ${PROJECT_NAME}-file-server DESTINATION ${CMAKE_INSTALL_BINDIR})
