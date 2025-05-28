# See LICENSE file in the project root for license information.

add_executable(${PROJECT_NAME}-rtty-server main.cpp)
target_link_libraries(${PROJECT_NAME}-rtty-server docopt::docopt_s_maybe ${PROJECT_NAME}::rtty ${PROJECT_NAME}::${PROJECT_NAME})
if(INSTALL_RPATH_KEYWORD)
  set_target_properties(${PROJECT_NAME}-rtty-server PROPERTIES ${INSTALL_RPATH_KEYWORD} ${RPATH_EXECUTABLE})
endif()
install(TARGETS ${PROJECT_NAME}-rtty-server DESTINATION ${CMAKE_INSTALL_BINDIR} PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE SETUID)
