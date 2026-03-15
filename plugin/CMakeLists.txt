# See LICENSE file in the project root for license information.

add_subdirectory(io-generic)
add_subdirectory(io-rstrm)

add_dependencies(${PROJECT_NAME} ${PROJECT_NAME}::plugin::io-rstrm ${PROJECT_NAME}::plugin::io-generic)

if(ENABLE_STATIC_PLUGINS)
  target_link_libraries(${PROJECT_NAME}-lib-static-plugins PUBLIC ${PROJECT_NAME}::plugin::io-rstrm ${PROJECT_NAME}::plugin::io-generic)
endif()
