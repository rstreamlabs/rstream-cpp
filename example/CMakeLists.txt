# See LICENSE file in the project root for license information.

add_subdirectory(core)
add_subdirectory(io)
add_subdirectory(io-rstrm)
if(BUILD_BINDING_PYTHON)
  add_subdirectory(python)
endif()
add_subdirectory(external)
