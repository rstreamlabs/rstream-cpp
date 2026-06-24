# See LICENSE file in the project root for license information.

add_executable(${PROJECT_NAME}-webtty-server main.cpp)
target_include_directories(${PROJECT_NAME}-webtty-server PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../common")
target_link_libraries(${PROJECT_NAME}-webtty-server docopt::docopt_s_maybe ${PROJECT_NAME}::webtty ${PROJECT_NAME}::${PROJECT_NAME} nlohmann_json::nlohmann_json yaml-cpp::yaml-cpp)
add_dependencies(${PROJECT_NAME}-webtty-server ${PROJECT_NAME}-plugin-io-generic ${PROJECT_NAME}-plugin-io-rstrm)
add_custom_command(
	TARGET ${PROJECT_NAME}-webtty-server POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${PROJECT_NAME}-webtty-server>/../lib/rstream/"
	COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:${PROJECT_NAME}-plugin-io-generic>" "$<TARGET_FILE_DIR:${PROJECT_NAME}-webtty-server>/../lib/rstream/"
	COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:${PROJECT_NAME}-plugin-io-rstrm>" "$<TARGET_FILE_DIR:${PROJECT_NAME}-webtty-server>/../lib/rstream/")
if(INSTALL_RPATH_KEYWORD)
	set_target_properties(${PROJECT_NAME}-webtty-server PROPERTIES ${INSTALL_RPATH_KEYWORD} ${RPATH_EXECUTABLE})
endif()
install(TARGETS ${PROJECT_NAME}-webtty-server DESTINATION ${CMAKE_INSTALL_BINDIR} PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE SETUID)
