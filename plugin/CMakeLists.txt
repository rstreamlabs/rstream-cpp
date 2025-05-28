# See LICENSE file in the project root for license information.

add_subdirectory(io-generic)
add_subdirectory(io-rstrm)

add_dependencies(${PROJECT_NAME} ${PROJECT_NAME}::plugin::io-rstrm ${PROJECT_NAME}::plugin::io-generic)

if (ENABLE_STATIC_PLUGINS)
  if(BUILD_SHARED_LIBS)
    if(${CMAKE_CXX_COMPILER_ID} MATCHES "GNU") 
      target_link_libraries(${PROJECT_NAME} INTERFACE "-Wl,--no-as-needed" $<TARGET_FILE:${PROJECT_NAME}::plugin::io-rstrm> $<TARGET_FILE:${PROJECT_NAME}::plugin::io-generic> "-Wl,--as-needed")
    else()
      target_link_libraries(${PROJECT_NAME} INTERFACE ${PROJECT_NAME}::plugin::io-rstrm ${PROJECT_NAME}::plugin::io-generic)
    endif()
  else()
    target_link_libraries(${PROJECT_NAME}-lib-static-plugins PRIVATE ${PROJECT_NAME}::plugin::io-rstrm ${PROJECT_NAME}::plugin::io-generic)
  endif()
endif()
