# See LICENSE file in the project root for license information.

find_program(CMAKE_CXX_CLANG_TIDY NAMES clang-tidy)
if(NOT CMAKE_CXX_CLANG_TIDY)
  message(SEND_ERROR "clang-tidy requested but executable not found")
endif()
