# See LICENSE file in the project root for license information.

foreach(variable IN ITEMS
    RSTREAM_RUNTIME_PLUGIN_SOURCE
    RSTREAM_RUNTIME_PLUGIN_DIRECTORY
    RSTREAM_RUNTIME_PLUGIN_LOCK_DIRECTORY)
  if(NOT DEFINED ${variable} OR "${${variable}}" STREQUAL "")
    message(FATAL_ERROR "${variable} is required")
  endif()
endforeach()

if(NOT EXISTS "${RSTREAM_RUNTIME_PLUGIN_SOURCE}")
  message(FATAL_ERROR "Runtime plugin does not exist: ${RSTREAM_RUNTIME_PLUGIN_SOURCE}")
endif()

get_filename_component(runtime_plugin_directory "${RSTREAM_RUNTIME_PLUGIN_DIRECTORY}" ABSOLUTE)
file(MAKE_DIRECTORY "${RSTREAM_RUNTIME_PLUGIN_LOCK_DIRECTORY}")
string(SHA256 runtime_plugin_directory_hash "${runtime_plugin_directory}")
set(lock_file "${RSTREAM_RUNTIME_PLUGIN_LOCK_DIRECTORY}/${runtime_plugin_directory_hash}.lock")
file(
  LOCK "${lock_file}"
  GUARD PROCESS
  TIMEOUT 120
  RESULT_VARIABLE lock_result)
if(NOT lock_result STREQUAL "0")
  message(FATAL_ERROR "Failed to lock runtime plugin directory: ${lock_result}")
endif()

file(MAKE_DIRECTORY "${runtime_plugin_directory}")
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different
    "${RSTREAM_RUNTIME_PLUGIN_SOURCE}"
    "${runtime_plugin_directory}/"
  RESULT_VARIABLE copy_result
  ERROR_VARIABLE copy_error)
if(NOT copy_result EQUAL 0)
  message(FATAL_ERROR "Failed to copy runtime plugin: ${copy_error}")
endif()
