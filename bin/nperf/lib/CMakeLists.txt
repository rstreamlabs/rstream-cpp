# See LICENSE file in the project root for license information.

# process subdirectories

add_subdirectory(cpp)
if(BUILD_BINDING_PYTHON)
  add_subdirectory(python)
endif()
