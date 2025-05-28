# See LICENSE file in the project root for license information.

# process subdirectories

if(BUILD_BINDING_PYTHON)
  add_subdirectory(gping)
endif()
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  add_subdirectory(inspect)
endif()
add_subdirectory(ncat)
add_subdirectory(nperf)
if(BUILD_BINDING_PYTHON)
  add_subdirectory(runpy)
endif()
add_subdirectory(rtty)
add_subdirectory(tunnel)
