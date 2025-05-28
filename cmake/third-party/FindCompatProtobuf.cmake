# See LICENSE file in the project root for license information.

find_package(Protobuf CONFIG QUIET)
if (NOT Protobuf_FOUND)
  find_package(Protobuf MODULE QUIET)
endif()
set(CompatProtobuf_FOUND ${Protobuf_FOUND})
if(NOT TARGET protobuf::libprotobuf)
  if(TARGET Protobuf::Protobuf)
    add_library(protobuf::libprotobuf INTERFACE IMPORTED)
    set_property(TARGET protobuf::libprotobuf PROPERTY INTERFACE_LINK_LIBRARIES Protobuf::Protobuf)
  endif()
endif()

if(NOT DEFINED Protobuf_PROTOC_EXECUTABLE)
  find_program(Protobuf_PROTOC_EXECUTABLE REQUIRED NAMES protoc)
endif()
