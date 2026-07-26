# See LICENSE file in the project root for license information.

add_subdirectory(io-generic)
add_subdirectory(io-rstrm)

add_dependencies(${PROJECT_NAME} ${PROJECT_NAME}::plugin::io-rstrm ${PROJECT_NAME}::plugin::io-generic)

if(ENABLE_STATIC_PLUGINS)
  target_link_libraries(${PROJECT_NAME}-lib-static-plugins PUBLIC ${PROJECT_NAME}::plugin::io-rstrm ${PROJECT_NAME}::plugin::io-generic)
  if(APPLE)
    target_link_libraries(
      ${PROJECT_NAME}
      INTERFACE
      "-Wl,-force_load,$<TARGET_FILE:${PROJECT_NAME}::static-plugins>"
      ${PROJECT_NAME}::static-plugins)
  elseif(MSVC)
    target_link_libraries(
      ${PROJECT_NAME}
      INTERFACE
      "/WHOLEARCHIVE:$<TARGET_FILE:${PROJECT_NAME}::static-plugins>"
      ${PROJECT_NAME}::static-plugins)
  else()
    target_link_libraries(
      ${PROJECT_NAME}
      INTERFACE
      "-Wl,--whole-archive,$<TARGET_FILE:${PROJECT_NAME}::static-plugins>,--no-whole-archive"
      ${PROJECT_NAME}::static-plugins)
  endif()
endif()
