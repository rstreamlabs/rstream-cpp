# See LICENSE file in the project root for license information.

# rpath

if(${BUILD_SHARED_LIBS})
  if("${CMAKE_SYSTEM_NAME}" STREQUAL "Darwin")
    set(RPATH_EXECUTABLE "@executable_path/../lib")
    set(RPATH_LIBRARY "@loader_path")
    set(RPATH_PLUGIN "@loader_path/..")
    set(INSTALL_RPATH_KEYWORD INSTALL_RPATH)
  elseif("${CMAKE_SYSTEM_NAME}" STREQUAL "Linux")
    set(RPATH_EXECUTABLE "\${ORIGIN}/../lib")
    set(RPATH_LIBRARY "\${ORIGIN}")
    set(RPATH_PLUGIN "\${ORIGIN}/..")
    set(INSTALL_RPATH_KEYWORD INSTALL_RPATH)
  endif()
endif()
