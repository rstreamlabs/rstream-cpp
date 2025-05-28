# See LICENSE file in the project root for license information.

# dependencies

include(FetchContent)

set(FETCHCONTENT_QUIET OFF)
set(FETCHCONTENT_BASE_DIR ${CMAKE_BINARY_DIR}/external)

# external projects

add_subdirectory(docopt.cpp)
if(ENABLE_TESTING)
  add_subdirectory(gtest)
endif()
