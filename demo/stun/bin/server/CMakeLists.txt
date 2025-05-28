# See LICENSE file in the project root for license information.

add_executable(${PROJECT_NAME}-stun-server main.cpp)
target_link_libraries(${PROJECT_NAME}-stun-server docopt::docopt_s_maybe ${PROJECT_NAME}::stun ${PROJECT_NAME}::${PROJECT_NAME})
if(INSTALL_RPATH_KEYWORD)
  set_target_properties(${PROJECT_NAME}-stun-server PROPERTIES ${INSTALL_RPATH_KEYWORD} ${RPATH_EXECUTABLE})
endif()
install(TARGETS ${PROJECT_NAME}-stun-server DESTINATION ${CMAKE_INSTALL_BINDIR})
