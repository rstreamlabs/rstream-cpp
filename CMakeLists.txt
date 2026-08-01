# See LICENSE file in the project root for license information.

# read version number from file

file(STRINGS ${CMAKE_CURRENT_SOURCE_DIR}/version.txt version_from_file)

# general information

cmake_minimum_required(VERSION 3.10)
if(${CMAKE_VERSION} VERSION_GREATER_EQUAL 3.13.0)
  cmake_policy(SET CMP0079 NEW)
endif()
if(POLICY CMP0095)
  cmake_policy(SET CMP0095 NEW)
endif()
if(POLICY CMP0169)
  cmake_policy(SET CMP0169 NEW)
endif()
if(POLICY CMP0156)
  cmake_policy(SET CMP0156 NEW)
endif()
if(POLICY CMP0179)
  cmake_policy(SET CMP0179 NEW)
endif()
project(rstream VERSION ${version_from_file} LANGUAGES C CXX)

# install destinations

include(GNUInstallDirs)

# CMake configuration

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake/third-party")

include(cmake/declarations.cmake)
include(cmake/utils.cmake)
include(cmake/dependencies.cmake)
include(cmake/options.cmake)
include(cmake/plugins.cmake)
include(cmake/warnings.cmake)
include(cmake/extra_flags.cmake)
if(BUILD_BINDING_PYTHON)
  include(cmake/python.cmake)
endif()
if(EMSCRIPTEN)
  include(cmake/emscripten.cmake)
endif()
if(NOT STATIC_LIBSTDCXX)
  include(cmake/rpath.cmake)
endif()
include(cmake/ccache.cmake)
if(ENABLE_TESTING)
  include(cmake/tests.cmake)
endif()
if(ENABLE_CLANG_TIDY)
  include(cmake/clang-tidy.cmake)
endif()
if(ENABLE_CPPCHECK)
  include(cmake/cppcheck.cmake)
endif()
if(ENABLE_INCLUDE_WHAT_YOU_USE)
  include(cmake/include-what-you-use.cmake)
endif()

# process subdirectories

add_subdirectory(external)
add_subdirectory(lib)
add_subdirectory(binding)
add_subdirectory(plugin)

if(NOT BUILD_SHARED_LIBS)
  if(STATIC_LIBSTDCXX)
    find_library(ATOMIC NAMES libatomic.a)
  else()
    find_library(ATOMIC NAMES atomic libatomic.so.1)
  endif()
  if(ATOMIC)
    target_link_libraries(${PROJECT_NAME} INTERFACE
      $<BUILD_INTERFACE:${ATOMIC}>
      $<INSTALL_INTERFACE:atomic>) # workaround for various missing dependencies on libatomic (mips, static linking)
  endif()
endif()

if(BUILD_BINS)
  add_subdirectory(bin)
endif()
if(BUILD_BENCHMARKS)
  add_subdirectory(benchmark)
endif()
if(BUILD_DEMOS)
  add_subdirectory(demo)
endif()
if(BUILD_EXAMPLES)
  add_subdirectory(example)
endif()
if(ENABLE_TESTING)
  add_subdirectory(test)
endif()

# apply project-only diagnostics after every target has been declared
rstream_enable_project_warnings("${PROJECT_SOURCE_DIR}")

# print summary

include(cmake/summary.cmake)

# run tests

if(ENABLE_TESTING)
  add_check_target(TESTS ${test_targets})
endif()
