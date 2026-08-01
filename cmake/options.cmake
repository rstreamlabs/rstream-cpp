# See LICENSE file in the project root for license information.

# generic options

include(CMakeDependentOption)

option(BUILD_BINS "Build binaries" ON)
option(DEAD_CODE_ELIMINATION "Eliminate dead code in libraries / binaries" OFF)
option(EMBED_DEFAULT_CA_CERTIFICATES "Embed default CA certificates in the library" ON)
option(ENABLE_CLANG_TIDY "Enable testing with 'clang-tidy'" OFF)
option(ENABLE_CPPCHECK "Enable testing with 'cppcheck'" OFF)
option(ENABLE_INCLUDE_WHAT_YOU_USE "Enable testing with 'include-what-you-use'" OFF)
option(ENABLE_STRICT_WARNINGS "Enable strict compiler warnings for rstream targets" OFF)
option(OPTIMIZE_FOR_NATIVE "Build with -march=native" OFF)
option(STATIC_LIBSTDCXX "Statically link with libc / libstdc++" OFF)
option(WARNINGS_AS_ERRORS "Treat rstream target warnings as errors" OFF)
option(WITH_IO_STREAMS "Build with IO streams support" ON)

if(WARNINGS_AS_ERRORS AND NOT ENABLE_STRICT_WARNINGS)
  message(FATAL_ERROR "WARNINGS_AS_ERRORS requires ENABLE_STRICT_WARNINGS=ON.")
endif()

cmake_dependent_option(BUILD_BENCHMARKS "Build benchmarks programs" ON "NOT CMAKE_BUILD_TYPE STREQUAL Release" OFF)
cmake_dependent_option(BUILD_DEMOS "Build demos programs" ON "NOT CMAKE_BUILD_TYPE STREQUAL Release" OFF)
cmake_dependent_option(BUILD_EXAMPLES "Build example programs" ON "NOT CMAKE_BUILD_TYPE STREQUAL Release" OFF)
set(ENABLE_TESTING_DEFAULT ON)
if(CMAKE_CROSSCOMPILING)
  set(ENABLE_TESTING_DEFAULT OFF)
endif()
option(ENABLE_TESTING "Build test programs" ${ENABLE_TESTING_DEFAULT})

set(ENABLE_STATIC_PLUGINS_DEFAULT ON)
if(BUILD_SHARED_LIBS)
  set(ENABLE_STATIC_PLUGINS_DEFAULT OFF)
endif()

cmake_dependent_option_strict(BUILD_BINDING_JAVA "Build JAVA binding" OFF "JNI_FOUND" OFF)
cmake_dependent_option_strict(BUILD_BINDING_PYTHON "Build Python binding" OFF "TARGET Boost::${BOOST_COMPONENT_PYTHON} AND TARGET Python3::Python" OFF)
option(ENABLE_STATIC_PLUGINS "Use static linking for plugins" ${ENABLE_STATIC_PLUGINS_DEFAULT})
cmake_dependent_option_strict(PYTHON_INSTALL_DEPENDENCIES "Install python dependencies" ON "${BUILD_BINDING_PYTHON}" OFF)
cmake_dependent_option_strict(PYTHON_INSTALL_STDLIB "Install python standard library" OFF "${BUILD_BINDING_PYTHON}" OFF)
cmake_dependent_option_strict(RSTREAM_WITH_PKCS11 "Build with PKCS#11 credential storage support" ON "SSL_PROVIDER STREQUAL openssl" OFF)
cmake_dependent_option_strict(WITH_MAXMINDDB "Build with maxminddb library support" ON "TARGET maxminddb::maxminddb" OFF)
cmake_dependent_option_strict(WITH_NCURSES "Build with ncurses library support" ON "TARGET Curses::Curses" OFF)
