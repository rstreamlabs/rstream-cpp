# See LICENSE file in the project root for license information.

add_executable(${PROJECT_NAME}-ncat main.cpp)
target_link_libraries(${PROJECT_NAME}-ncat docopt::docopt_s_maybe ${PROJECT_NAME}::ncat ${PROJECT_NAME}::${PROJECT_NAME})
add_dependencies(${PROJECT_NAME}-ncat ${PROJECT_NAME}-plugin-io-generic ${PROJECT_NAME}-plugin-io-rstrm)
add_custom_command(
  TARGET ${PROJECT_NAME}-ncat POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${PROJECT_NAME}-ncat>/../lib/rstream/"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:${PROJECT_NAME}-plugin-io-generic>" "$<TARGET_FILE_DIR:${PROJECT_NAME}-ncat>/../lib/rstream/"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:${PROJECT_NAME}-plugin-io-rstrm>" "$<TARGET_FILE_DIR:${PROJECT_NAME}-ncat>/../lib/rstream/")
if(INSTALL_RPATH_KEYWORD)
  set_target_properties(${PROJECT_NAME}-ncat PROPERTIES ${INSTALL_RPATH_KEYWORD} ${RPATH_EXECUTABLE})
endif()
install(TARGETS ${PROJECT_NAME}-ncat DESTINATION ${CMAKE_INSTALL_BINDIR})
