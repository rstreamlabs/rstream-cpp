# See LICENSE file in the project root for license information.

# docopt.cpp

if(NOT docopt_FOUND)
  FetchContent_Declare(
    docopt.cpp
    GIT_REPOSITORY "https://github.com/docopt/docopt.cpp.git"
    GIT_TAG "v0.6.3"
    UPDATE_DISCONNECTED TRUE)
  set(CMAKE_POLICY_VERSION_MINIMUM 3.10)
  FetchContent_MakeAvailable(docopt.cpp)
  unset(CMAKE_POLICY_VERSION_MINIMUM)
elseif(TARGET docopt_s)
  get_property(DOCOPT_INCLUDE_DIRECTORIES TARGET docopt_s PROPERTY INTERFACE_INCLUDE_DIRECTORIES)
  find_file(DOCOPT_H NAMES docopt.h PATHS ${DOCOPT_INCLUDE_DIRECTORIES})
  if(NOT DOCOPT_H)
    set_property(TARGET docopt_s APPEND_STRING PROPERTY INTERFACE_INCLUDE_DIRECTORIES "/docopt")
  endif()
endif()

if(NOT TARGET docopt::docopt_s)
  if(TARGET docopt_s)
    add_library(docopt::docopt_s INTERFACE IMPORTED GLOBAL)
    set_property(TARGET docopt::docopt_s PROPERTY INTERFACE_LINK_LIBRARIES docopt_s)
  endif()
endif()

if(NOT TARGET docopt::docopt)
  if(TARGET docopt)
    add_library(docopt::docopt INTERFACE IMPORTED GLOBAL)
    set_property(TARGET docopt::docopt PROPERTY INTERFACE_LINK_LIBRARIES docopt)
  endif()
endif()

if(TARGET docopt::docopt_s)
  add_library(docopt::docopt_s_maybe INTERFACE IMPORTED GLOBAL)
  set_property(TARGET docopt::docopt_s_maybe PROPERTY INTERFACE_LINK_LIBRARIES docopt::docopt_s)
elseif(TARGET docopt::docopt)
  add_library(docopt::docopt_s_maybe INTERFACE IMPORTED GLOBAL)
  set_property(TARGET docopt::docopt_s_maybe PROPERTY INTERFACE_LINK_LIBRARIES docopt::docopt)
else()
  message(FATAL_ERROR "docopt not found")
endif()
