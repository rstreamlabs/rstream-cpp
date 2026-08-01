# See LICENSE file in the project root for license information.

function(rstream_enable_runtime_plugins target)
  set(runtime_plugins)
  foreach(plugin IN LISTS ARGN)
    get_target_property(plugin_type ${plugin} TYPE)
    if(plugin_type STREQUAL "MODULE_LIBRARY")
      list(APPEND runtime_plugins ${plugin})
    elseif(NOT plugin_type STREQUAL "STATIC_LIBRARY")
      message(FATAL_ERROR "Unsupported rstream plugin target type: ${plugin_type}")
    endif()
  endforeach()
  if(NOT runtime_plugins)
    return()
  endif()
  get_target_property(core_type rstream::core TYPE)
  if(core_type STREQUAL "STATIC_LIBRARY")
    set_target_properties(${target} PROPERTIES ENABLE_EXPORTS ON)
  endif()
  set(plugin_directory "$<TARGET_FILE_DIR:${target}>/../lib/rstream")
  add_custom_command(
    TARGET ${target}
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${plugin_directory}")
  foreach(plugin IN LISTS runtime_plugins)
    add_dependencies(${target} ${plugin})
    add_custom_command(
      TARGET ${target}
      POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:${plugin}>" "${plugin_directory}/")
  endforeach()
endfunction()
