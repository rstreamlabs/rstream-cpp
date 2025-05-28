# See LICENSE file in the project root for license information.

# utilities

include(GenerateExportHeader)
include(CMakePackageConfigHelpers)

# build info

if(EXISTS ${PROJECT_SOURCE_DIR}/.git/)
  find_package(Git)
  if(Git_FOUND)
    execute_process(
      COMMAND ${GIT_EXECUTABLE} -C ${PROJECT_SOURCE_DIR} rev-parse --abbrev-ref HEAD
      OUTPUT_VARIABLE RSTREAM_GIT_BRANCH)
    string(REGEX REPLACE "\n$" "" RSTREAM_GIT_BRANCH "${RSTREAM_GIT_BRANCH}")
    execute_process(
      COMMAND ${GIT_EXECUTABLE} -C ${PROJECT_SOURCE_DIR} rev-parse HEAD
      OUTPUT_VARIABLE RSTREAM_GIT_COMMIT)
    string(REGEX REPLACE "\n$" "" RSTREAM_GIT_COMMIT "${RSTREAM_GIT_COMMIT}")
    execute_process(
      COMMAND ${GIT_EXECUTABLE} -C ${PROJECT_SOURCE_DIR} diff-index --quiet HEAD --
      RESULT_VARIABLE RSTREAM_GIT_UNSTAGED_CHANGES)
  endif()
endif()

if(NOT DEFINED Git_FOUND)
  set(Git_FOUND FALSE)
endif()

if(NOT EXISTS ${CMAKE_CURRENT_BINARY_DIR}/config.hpp)
  string(TIMESTAMP RSTREAM_BUILD_DATE "%Y-%m-%d %H:%M:%S UTC" UTC)
  configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/config.hpp.in
    ${CMAKE_CURRENT_BINARY_DIR}/config.hpp
    @ONLY)
endif()

# build components

add_subdirectory(core)
if(ENABLE_STATIC_PLUGINS)
  add_subdirectory(static-plugins)
endif()
add_subdirectory(io)
add_subdirectory(io-rstrm)

# add interface library

add_library(${PROJECT_NAME} INTERFACE)
add_library(${PROJECT_NAME}::${PROJECT_NAME} ALIAS ${PROJECT_NAME})

target_include_directories(
  ${PROJECT_NAME}
  INTERFACE
  $<INSTALL_INTERFACE:$<INSTALL_PREFIX>/include/>)

target_link_libraries(
  ${PROJECT_NAME}
  INTERFACE
  ${PROJECT_NAME}::io-rstrm
  ${PROJECT_NAME}::io
  ${PROJECT_NAME}::core)

install(TARGETS ${PROJECT_NAME} EXPORT ${CMAKE_PACKAGE_TARGETS})

# install headers

install_headers_prefix("${CMAKE_CURRENT_SOURCE_DIR}/${PROJECT_NAME}.hpp" "${CMAKE_INSTALL_INCLUDEDIR}" "${PROJECT_SOURCE_DIR}/lib/")
install_headers_prefix("${CMAKE_CURRENT_BINARY_DIR}/config.hpp" "${CMAKE_INSTALL_INCLUDEDIR}" "${PROJECT_BINARY_DIR}/lib/")

# configure the library

if(NOT DEFINED INSTALL_PATH_CMAKE_CONFIG)
  set(INSTALL_PATH_CMAKE_CONFIG ${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME})
endif()

write_basic_package_version_file(
  ${CMAKE_PACKAGE_NAME}ConfigVersion.cmake
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY SameMajorVersion)

export(EXPORT ${CMAKE_PACKAGE_TARGETS}
  FILE ${CMAKE_PACKAGE_TARGETS}.cmake
  NAMESPACE ${PROJECT_NAME}::)

configure_package_config_file(
  ${CMAKE_PACKAGE_NAME}Config.cmake.in
  ${CMAKE_PACKAGE_NAME}Config.cmake
  INSTALL_DESTINATION ${INSTALL_PATH_CMAKE_CONFIG})

install(EXPORT ${CMAKE_PACKAGE_TARGETS}
  FILE ${CMAKE_PACKAGE_TARGETS}.cmake
  NAMESPACE ${PROJECT_NAME}::
  DESTINATION ${INSTALL_PATH_CMAKE_CONFIG})
  
file(GLOB_RECURSE CMAKE_MODULE_FILES ${PROJECT_SOURCE_DIR}/cmake/third-party/Find*.cmake)
install(FILES ${CMAKE_MODULE_FILES} DESTINATION ${INSTALL_PATH_CMAKE_CONFIG})

install(FILES
  ${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_PACKAGE_NAME}Config.cmake
  ${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_PACKAGE_NAME}ConfigVersion.cmake
  DESTINATION ${INSTALL_PATH_CMAKE_CONFIG}
  COMPONENT Devel)
