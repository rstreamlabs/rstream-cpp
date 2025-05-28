# See LICENSE file in the project root for license information.

find_program(CMAKE_CXX_INCLUDE_WHAT_YOU_USE NAMES include-what-you-use iwyu)
if(NOT CMAKE_CXX_INCLUDE_WHAT_YOU_USE)
  message(SEND_ERROR "include-what-you-use requested but executable not found")
endif()
