# See LICENSE file in the project root for license information.

add_executable(${PROJECT_NAME}-webtty-client main.cpp)
target_include_directories(${PROJECT_NAME}-webtty-client PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../common")
target_link_libraries(${PROJECT_NAME}-webtty-client docopt::docopt_s_maybe ${PROJECT_NAME}::webtty ${PROJECT_NAME}::${PROJECT_NAME} nlohmann_json::nlohmann_json yaml-cpp::yaml-cpp)
add_dependencies(${PROJECT_NAME}-webtty-client ${PROJECT_NAME}-plugin-io-generic ${PROJECT_NAME}-plugin-io-rstrm)
add_custom_command(
	TARGET ${PROJECT_NAME}-webtty-client POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${PROJECT_NAME}-webtty-client>/../lib/rstream/"
	COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:${PROJECT_NAME}-plugin-io-generic>" "$<TARGET_FILE_DIR:${PROJECT_NAME}-webtty-client>/../lib/rstream/"
	COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:${PROJECT_NAME}-plugin-io-rstrm>" "$<TARGET_FILE_DIR:${PROJECT_NAME}-webtty-client>/../lib/rstream/")
if(INSTALL_RPATH_KEYWORD)
	set_target_properties(${PROJECT_NAME}-webtty-client PROPERTIES ${INSTALL_RPATH_KEYWORD} ${RPATH_EXECUTABLE})
endif()
install(TARGETS ${PROJECT_NAME}-webtty-client DESTINATION ${CMAKE_INSTALL_BINDIR})
