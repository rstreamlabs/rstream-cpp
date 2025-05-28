# See LICENSE file in the project root for license information.

if(BUILD_BINDING_JAVA)
  add_subdirectory(java)
endif()
if(BUILD_BINDING_PYTHON)
  add_subdirectory(python)
endif()
