# See LICENSE file in the project root for license information.

# global declarations

set(PROJECT_DESCRIPTION "rstream")
set(CMAKE_PACKAGE_NAME "${PROJECT_NAME}")
set(CMAKE_PACKAGE_TARGETS "${CMAKE_PACKAGE_NAME}Targets")
set(SOVERSION ${PROJECT_VERSION_MAJOR})
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(PROJECT_VERSION_REL_TYPE "${PROJECT_VERSION}-${PROJECT_RELEASE_TYPE}")
set(CMAKE_INSTALL_PLUGINDIR "${CMAKE_INSTALL_LIBDIR}/${PROJECT_NAME}")

# licence

set(RSTREAM_COPYING "Apache License 2.0")

# CXX declarations

if (WIN32)
  add_compile_definitions(_WIN32_WINNT=0x0A00)
  add_compile_definitions(NTDDI_VERSION=0x0A000006)
  if(MSVC)
    add_compile_options(/bigobj)
    add_compile_options(/Zc:preprocessor) # support for __VA_ARGS__ in macros
  elseif (MINGW)
    add_compile_options(-Wa,-mbig-obj)
    link_libraries(ws2_32 mswsock)
    if(BUILD_SHARED_LIBS)
      add_link_options(-Wl,--allow-multiple-definition)
    endif()
  endif()
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(CheckCompilerFlag)
if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
  check_compiler_flag(CXX "-fcoroutines-ts" COMPILER_SUPPORTS_COROUTINES)
  if(COMPILER_SUPPORTS_COROUTINES)
    add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-fcoroutines-ts>)
  endif()
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  check_compiler_flag(CXX "-fcoroutines" COMPILER_SUPPORTS_COROUTINES)
  if(COMPILER_SUPPORTS_COROUTINES)
    add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-fcoroutines>)
  endif()
endif()

if(DEFINED RSTREAM_BUILD_OS AND NOT RSTREAM_BUILD_OS STREQUAL "")
  add_compile_definitions(RSTREAM_BUILD_OS=\"${RSTREAM_BUILD_OS}\")
endif()

if(DEFINED RSTREAM_BUILD_ARCH AND NOT RSTREAM_BUILD_ARCH STREQUAL "")
  add_compile_definitions(RSTREAM_BUILD_ARCH=\"${RSTREAM_BUILD_ARCH}\")
endif()

if(DEFINED RSTREAM_BUILD_CHANNEL AND NOT RSTREAM_BUILD_CHANNEL STREQUAL "")
  add_compile_definitions(RSTREAM_BUILD_CHANNEL=\"${RSTREAM_BUILD_CHANNEL}\")
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Debug" OR ${CMAKE_GENERATOR} STREQUAL Xcode)
  add_compile_definitions(DEBUG_BUILD)
endif()
