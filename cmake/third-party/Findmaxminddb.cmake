# See LICENSE file in the project root for license information.

include(FindPackageHandleStandardArgs)

find_package(PkgConfig QUIET)

if(PkgConfig_FOUND)
  pkg_check_modules(PC_maxminddb QUIET libmaxminddb)
endif()

find_path(maxminddb_INCLUDE_DIR
  NAMES maxminddb.h
  HINTS ${PC_maxminddb_INCLUDE_DIRS})

find_library(maxminddb_LIBRARY
  NAMES maxminddb
  HINTS ${PC_maxminddb_LIBRARY_DIRS})

find_package_handle_standard_args(maxminddb
  REQUIRED_VARS maxminddb_INCLUDE_DIR maxminddb_LIBRARY)

if(maxminddb_FOUND AND NOT TARGET maxminddb::maxminddb)
  add_library(maxminddb::maxminddb INTERFACE IMPORTED GLOBAL)
  set_property(TARGET maxminddb::maxminddb PROPERTY INTERFACE_INCLUDE_DIRECTORIES "${maxminddb_INCLUDE_DIR}")
  set_property(TARGET maxminddb::maxminddb PROPERTY INTERFACE_LINK_LIBRARIES "${maxminddb_LIBRARY}")
endif()
