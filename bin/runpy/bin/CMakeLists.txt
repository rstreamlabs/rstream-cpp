# See LICENSE file in the project root for license information.

add_executable(${PROJECT_NAME}-runpy main.cpp)
file(RELATIVE_PATH PYTHON3_SITEPACKAGES ${CMAKE_INSTALL_FULL_BINDIR} ${CMAKE_INSTALL_Python3_SITEPACKAGES})
target_compile_definitions(${PROJECT_NAME}-runpy PRIVATE -DPYTHON3_SITEPACKAGES="${PYTHON3_SITEPACKAGES}")
target_link_libraries(${PROJECT_NAME}-runpy docopt::docopt_s_maybe Python3::Python ${PROJECT_NAME}::binding::python ${PROJECT_NAME}::${PROJECT_NAME})
if(INSTALL_RPATH_KEYWORD)
  set_target_properties(${PROJECT_NAME}-runpy PROPERTIES ${INSTALL_RPATH_KEYWORD} ${RPATH_EXECUTABLE})
endif()
install(TARGETS ${PROJECT_NAME}-runpy DESTINATION ${CMAKE_INSTALL_BINDIR})
