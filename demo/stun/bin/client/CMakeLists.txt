# See LICENSE file in the project root for license information.

add_executable(${PROJECT_NAME}-stun-client client.cpp client.hpp main.cpp)
target_link_libraries(${PROJECT_NAME}-stun-client docopt::docopt_s_maybe ${PROJECT_NAME}::stun ${PROJECT_NAME}::${PROJECT_NAME})
if(INSTALL_RPATH_KEYWORD)
  set_target_properties(${PROJECT_NAME}-stun-client PROPERTIES ${INSTALL_RPATH_KEYWORD} ${RPATH_EXECUTABLE})
endif()
install(TARGETS ${PROJECT_NAME}-stun-client DESTINATION ${CMAKE_INSTALL_BINDIR})
