# See LICENSE file in the project root for license information.

find_program(CMAKE_CXX_CPPCHECK NAMES cppcheck)
if(CMAKE_CXX_CPPCHECK)
  list(APPEND CMAKE_CXX_CPPCHECK
    "--enable=warning"
    "--inconclusive"
    "--force"
    "--inline-suppr")
else()
  message(SEND_ERROR "cppcheck requested but executable not found")
endif()
