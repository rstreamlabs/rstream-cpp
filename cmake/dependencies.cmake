# See LICENSE file in the project root for license information.

# dependencies

include(FindPkgConfig)

set(SSL_PROVIDER "openssl" CACHE STRING "SSL provider to use")
if(EMSCRIPTEN)
  find_package(Boost "1.81.0")
else()
  find_package(Boost "1.81.0" REQUIRED COMPONENTS filesystem date_time regex url random OPTIONAL_COMPONENTS coroutine thread)
endif()
find_package(CompatProtobuf REQUIRED)
find_package(docopt QUIET)
find_package(GTest QUIET)
if(NOT DEFINED BUILD_BINDING_JAVA OR BUILD_BINDING_JAVA)
  find_package(JNI QUIET)
endif()
find_package(maxminddb QUIET)
if(NOT maxminddb_FOUND)
  pkg_check_modules(libmaxminddb IMPORTED_TARGET libmaxminddb)
  if(TARGET PkgConfig::libmaxminddb)
    add_library(maxminddb::maxminddb INTERFACE IMPORTED GLOBAL)
    set_property(TARGET maxminddb::maxminddb PROPERTY INTERFACE_LINK_LIBRARIES PkgConfig::libmaxminddb)
  endif()
endif()
find_package(Curses QUIET)
if(CURSES_FOUND AND NOT TARGET Curses::Curses)
  add_library(Curses::Curses INTERFACE IMPORTED GLOBAL)
  set_property(TARGET Curses::Curses PROPERTY INTERFACE_INCLUDE_DIRECTORIES ${CURSES_INCLUDE_DIRS})
  set_property(TARGET Curses::Curses PROPERTY INTERFACE_LINK_LIBRARIES ${CURSES_LIBRARIES})
endif()
find_package(nlohmann_json REQUIRED)
if(SSL_PROVIDER STREQUAL "openssl")
  if(APPLE)
    if(NOT OpenSSL_DIR)
      find_program(HOMEBREW brew)
      if(HOMEBREW STREQUAL "HOMEBREW-NOTFOUND")
        message(WARNING "homebrew not found : not using homebrew's OpenSSL version")
        if(NOT OPENSSL_ROOT_DIR)
          message(WARNING "use -DOPENSSL_ROOT_DIR for non-Apple OpenSSL")
        endif()
      else()
        execute_process(COMMAND brew --prefix openssl@3
          OUTPUT_VARIABLE OPENSSL_ROOT_DIR
          OUTPUT_STRIP_TRAILING_WHITESPACE)
      endif()
    endif()
  endif()
  find_package(OpenSSL REQUIRED)
  set(SSL_LIBRARIES OpenSSL::SSL OpenSSL::Crypto)
elseif(SSL_PROVIDER STREQUAL "libressl")
  find_package(LibreSSL REQUIRED)
  set(SSL_LIBRARIES LibreSSL::SSL LibreSSL::Crypto)
else()
  message(FATAL_ERROR "Unknown SSL provider: ${SSL_PROVIDER}")
endif()
if(NOT DEFINED BUILD_BINDING_PYTHON OR BUILD_BINDING_PYTHON)
  find_package(Python3 COMPONENTS Interpreter Development)
  set(BOOST_COMPONENT_PYTHON_CANDIDATES
    python${Python3_VERSION_MAJOR}${Python3_VERSION_MINOR}
    python${Python3_VERSION_MAJOR}
    python)
  foreach(candidate IN LISTS BOOST_COMPONENT_PYTHON_CANDIDATES)
    find_package(Boost QUIET OPTIONAL_COMPONENTS ${candidate})
    if(TARGET Boost::${candidate})
      set(BOOST_COMPONENT_PYTHON ${candidate})
      break()
    endif()
  endforeach()
  if(NOT BOOST_COMPONENT_PYTHON)
    message(WARNING "Boost Python not found")
  endif()
endif()
find_package(spdlog REQUIRED)
find_package(Threads REQUIRED)
find_package(yaml-cpp REQUIRED)

# extra libraries

include(CheckAtomic)

# additional configuration

# TODO : Replace boost::asio::deadline_timer with boost::asio::system_timer
# add_compile_definitions(BOOST_ASIO_NO_DEPRECATED)

if(MSVC)
  add_compile_definitions(BOOST_ALL_NO_LIB)
endif()
