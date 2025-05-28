# See LICENSE file in the project root for license information.

# library name

set(LIBRARY_COMPONENT "common")
set(LIBRARY_NAME "${PROJECT_NAME}-python-module-${LIBRARY_COMPONENT}")

# library sources

file(GLOB_RECURSE SOURCES *.cpp)
file(GLOB_RECURSE HEADERS *.hpp)

# library definition

add_library(${LIBRARY_NAME} MODULE ${SOURCES} ${HEADERS})
add_library(${PROJECT_NAME}::python::module::${LIBRARY_COMPONENT} ALIAS ${LIBRARY_NAME})

set_target_properties(
  ${LIBRARY_NAME}
  PROPERTIES
  OUTPUT_NAME ${LIBRARY_COMPONENT})

set_target_properties(
  ${LIBRARY_NAME}
  PROPERTIES
  PREFIX "")
  
target_include_directories(
  ${LIBRARY_NAME}
  PUBLIC
  $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/binding/python/lib/python/native/>)

target_link_libraries(
  ${LIBRARY_NAME}
  PUBLIC
  Boost::boost
  Boost::${BOOST_COMPONENT_PYTHON}
  Python3::Python)

# install the library

install(TARGETS ${LIBRARY_NAME} LIBRARY DESTINATION ${CMAKE_INSTALL_Python3_SITEPACKAGES}/${PROJECT_NAME})

# Xcode source group

if(${CMAKE_GENERATOR} STREQUAL Xcode)
  get_target_property(TARGET_SOURCES ${LIBRARY_NAME} SOURCES)
  source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${TARGET_SOURCES})
endif()
