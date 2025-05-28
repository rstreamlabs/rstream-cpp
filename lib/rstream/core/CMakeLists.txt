# See LICENSE file in the project root for license information.

# library name

set(LIBRARY_COMPONENT "core")
set(LIBRARY_NAME "${PROJECT_NAME}-lib-${LIBRARY_COMPONENT}")

# library sources

file(GLOB_RECURSE SOURCES *.cpp)
file(GLOB_RECURSE HEADERS *.hpp)

# library definition

add_library(${LIBRARY_NAME} ${SOURCES} ${HEADERS})
add_library(${PROJECT_NAME}::${LIBRARY_COMPONENT} ALIAS ${LIBRARY_NAME})

set_target_properties(
  ${LIBRARY_NAME}
  PROPERTIES
  VERSION ${PROJECT_VERSION}
  OUTPUT_NAME ${PROJECT_NAME}-${LIBRARY_COMPONENT}
  EXPORT_NAME ${LIBRARY_COMPONENT}
  SOVERSION ${SOVERSION}
  INTERFACE_${PROJECT_NAME}_MAJOR_VERSION ${SOVERSION}
  APPEND PROPERTY COMPATIBLE_INTERFACE_STRING ${PROJECT_NAME}_MAJOR_VERSION)

if(INSTALL_RPATH_KEYWORD)
  set_target_properties(${LIBRARY_NAME} PROPERTIES ${INSTALL_RPATH_KEYWORD} ${RPATH_LIBRARY})
endif()

target_include_directories(
  ${LIBRARY_NAME}
  PUBLIC
  $<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/lib/>
  $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/lib/>
  $<INSTALL_INTERFACE:$<INSTALL_PREFIX>/include/>)

if("${CMAKE_SYSTEM_NAME}" STREQUAL "Linux")
  target_link_libraries(${LIBRARY_NAME} PUBLIC atomic)
endif()

target_link_libraries(
  ${LIBRARY_NAME}
  PUBLIC
  Boost::boost
  nlohmann_json::nlohmann_json
  protobuf::libprotobuf
  spdlog::spdlog
  PRIVATE
  ${CMAKE_DL_LIBS}
  ${SSL_LIBRARIES}
  Boost::filesystem
  Boost::random)

# install the library

install(TARGETS ${LIBRARY_NAME}
  EXPORT ${CMAKE_PACKAGE_TARGETS}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

# install headers

install_headers("${HEADERS}" "${CMAKE_INSTALL_INCLUDEDIR}")

# Xcode source group

if(${CMAKE_GENERATOR} STREQUAL Xcode)
  get_target_property(TARGET_SOURCES ${LIBRARY_NAME} SOURCES)
  source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${TARGET_SOURCES})
endif()

# generate includes.hpp header file

set(INCLUDES_FILE "${CMAKE_CURRENT_BINARY_DIR}/includes.hpp")

add_custom_command(
  OUTPUT ${INCLUDES_FILE}
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  COMMAND ${CMAKE_COMMAND} -DOUTPUT_FILE=${INCLUDES_FILE} -DHEADERS_DIR=${CMAKE_CURRENT_SOURCE_DIR} -P ${PROJECT_SOURCE_DIR}/cmake/generate-includes-file.cmake
  COMMENT "Generating includes.hpp header file")

add_custom_target(GenerateIncludesFile${LIBRARY_NAME} DEPENDS ${INCLUDES_FILE})

add_dependencies(${LIBRARY_NAME} GenerateIncludesFile${LIBRARY_NAME})

install_headers_prefix("${INCLUDES_FILE}" "${CMAKE_INSTALL_INCLUDEDIR}" "${PROJECT_BINARY_DIR}/lib/")
