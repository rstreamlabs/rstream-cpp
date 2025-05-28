# See LICENSE file in the project root for license information.

# process subdirectories

if(NOT WIN32)
  add_subdirectory(client)
endif()
add_subdirectory(server)
