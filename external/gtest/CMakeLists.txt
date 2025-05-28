# See LICENSE file in the project root for license information.

# gtest

if(NOT GTest_FOUND)
  FetchContent_Declare(
    gtest
    GIT_REPOSITORY "https://github.com/google/googletest.git"
    GIT_TAG "release-1.12.1"
    UPDATE_DISCONNECTED TRUE)
    FetchContent_GetProperties(gtest)
    if(NOT gtest_POPULATED)
      FetchContent_Populate(gtest)
      add_subdirectory(${gtest_SOURCE_DIR} ${gtest_BINARY_DIR} EXCLUDE_FROM_ALL)
    endif()
endif()

if(NOT TARGET GTest::GTest)
  if(TARGET gtest)
    add_library(GTest::GTest INTERFACE IMPORTED GLOBAL)
    set_property(TARGET GTest::GTest PROPERTY INTERFACE_LINK_LIBRARIES gtest)
  endif()
endif()
