# See LICENSE file in the project root for license information.

if("${CMAKE_SYSTEM_NAME}" STREQUAL "Linux")
  find_program(CCACHE ccache PATHS /usr/lib/ccache)
elseif("${CMAKE_SYSTEM_NAME}" STREQUAL "Darwin")
  find_program(CCACHE ccache PATHS /usr/local/opt/ccache/libexec)
else()
  find_program(CCACHE ccache)
endif()

if(CCACHE)
  set(CMAKE_CXX_COMPILER_LAUNCHER ${CCACHE})
else()
  message(STATUS "ccache not found (skipping)")
endif()
