# See LICENSE file in the project root for license information.

# library name

set(LIBRARY_COMPONENT "io-generic")
set(LIBRARY_NAME "${PROJECT_NAME}-plugin-${LIBRARY_COMPONENT}")

# library sources

file(GLOB_RECURSE SOURCES *.cpp)
file(GLOB_RECURSE HEADERS *.hpp)

# library definition

add_library(${LIBRARY_NAME} ${PLUGIN_LIBRARY_TYPE} ${SOURCES} ${HEADERS})
add_library(${PROJECT_NAME}::plugin::${LIBRARY_COMPONENT} ALIAS ${LIBRARY_NAME})

set_target_properties(
  ${LIBRARY_NAME}
  PROPERTIES
  OUTPUT_NAME ${PROJECT_NAME}-plugin-${LIBRARY_COMPONENT}
  EXPORT_NAME plugin::${LIBRARY_COMPONENT})

if(NOT ENABLE_STATIC_PLUGINS)
  set_target_properties(
    ${LIBRARY_NAME}
    PROPERTIES
    PREFIX "")
endif()

if(ENABLE_STATIC_PLUGINS)
  target_include_directories(
    ${LIBRARY_NAME}
    PRIVATE
    $<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/lib/>
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/lib/>)
  target_link_libraries(
    ${LIBRARY_NAME}
    PUBLIC
    Boost::boost
    Boost::filesystem
    ${PROJECT_NAME}::core
    ${PROJECT_NAME}::io)
else()
  target_link_libraries(
    ${LIBRARY_NAME}
    PUBLIC
    Boost::boost
    Boost::filesystem
    ${PROJECT_NAME}::core
    ${PROJECT_NAME}::io)
endif()

# install the library

if(ENABLE_STATIC_PLUGINS)
  if(INSTALL_RPATH_KEYWORD)
    set_target_properties(${LIBRARY_NAME} PROPERTIES ${INSTALL_RPATH_KEYWORD} ${RPATH_LIBRARY})
  endif()
  install(TARGETS ${LIBRARY_NAME} EXPORT ${CMAKE_PACKAGE_TARGETS} ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
else()
  if(INSTALL_RPATH_KEYWORD)
    set_target_properties(${LIBRARY_NAME} PROPERTIES ${INSTALL_RPATH_KEYWORD} ${RPATH_PLUGIN})
  endif()
  install(TARGETS ${LIBRARY_NAME} EXPORT ${CMAKE_PACKAGE_TARGETS} LIBRARY DESTINATION ${CMAKE_INSTALL_PLUGINDIR})
endif()

# Xcode source group

if(${CMAKE_GENERATOR} STREQUAL Xcode)
  get_target_property(TARGET_SOURCES ${LIBRARY_NAME} SOURCES)
  source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${TARGET_SOURCES})
endif()
