# See LICENSE file in the project root for license information.

# process subdirectories

add_subdirectory(lib)
add_subdirectory(bin)
if(ENABLE_TESTING)
  add_subdirectory(test)
endif()
