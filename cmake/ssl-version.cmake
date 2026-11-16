# See LICENSE file in the project root for license information.

function(rstream_validate_openssl_version version)
  if(version VERSION_LESS "3.5.9" OR
     (version VERSION_GREATER_EQUAL "3.6" AND version VERSION_LESS "3.6.5") OR
     version VERSION_GREATER_EQUAL "4")
    message(FATAL_ERROR "Unsupported OpenSSL ${version}: use 3.5.9+ in 3.5 LTS or 3.6.5+ below 4.")
  endif()
endfunction()
