# See LICENSE file in the project root for license information.

# generate includes.hpp file

function(generate_includes_file output_file headers_dir)
  file(GLOB HEADER_FILES "${headers_dir}/*.hpp")
  if(DEFINED EXCLUDE_REGEX AND NOT "${EXCLUDE_REGEX}" STREQUAL "")
    list(FILTER HEADER_FILES EXCLUDE REGEX "${EXCLUDE_REGEX}")
  endif()
  list(SORT HEADER_FILES)
  file(WRITE ${output_file} "// Auto-generated includes.hpp\n")
  file(APPEND ${output_file} "// Do not edit manually.\n\n")
  file(APPEND ${output_file} "#pragma once\n\n")
  foreach(header ${HEADER_FILES})
    get_filename_component(header_name ${header} NAME)
    if(NOT header_name STREQUAL "includes.hpp")
      file(APPEND ${output_file} "#include \"${header_name}\"\n")
    endif()
  endforeach()
endfunction()

generate_includes_file("${OUTPUT_FILE}" "${HEADERS_DIR}")
