# See LICENSE file in the project root for license information.

# process tests

set(RTTY_GENERATED_SOURCE_DIR "${PROJECT_BINARY_DIR}/bin/rtty/lib/rstream/rtty/generated-cpp-sources")

add_test_target(${PROJECT_NAME}-test-rtty-protocol test_rtty_protocol.cpp)
target_link_libraries(${PROJECT_NAME}-test-rtty-protocol ${PROJECT_NAME}::rtty Boost::url)

add_test_target(${PROJECT_NAME}-test-rtty-convert test_rtty_convert.cpp)
target_include_directories(${PROJECT_NAME}-test-rtty-convert PRIVATE ${RTTY_GENERATED_SOURCE_DIR})
target_link_libraries(${PROJECT_NAME}-test-rtty-convert ${PROJECT_NAME}::rtty protobuf::libprotobuf)

add_test_target(${PROJECT_NAME}-test-rtty-terminal-stream test_rtty_terminal_stream.cpp)
target_link_libraries(${PROJECT_NAME}-test-rtty-terminal-stream ${PROJECT_NAME}::rtty)

add_test_target(${PROJECT_NAME}-test-rtty-plain-server-runtime test_rtty_plain_server_runtime.cpp)
target_include_directories(${PROJECT_NAME}-test-rtty-plain-server-runtime PRIVATE ${RTTY_GENERATED_SOURCE_DIR})
target_link_libraries(${PROJECT_NAME}-test-rtty-plain-server-runtime ${PROJECT_NAME}::rtty protobuf::libprotobuf)
add_dependencies(${PROJECT_NAME}-test-rtty-plain-server-runtime ${PROJECT_NAME}-plugin-io-generic)
add_custom_command(TARGET ${PROJECT_NAME}-test-rtty-plain-server-runtime POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${PROJECT_NAME}-test-rtty-plain-server-runtime>/../lib/rstream"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:${PROJECT_NAME}-plugin-io-generic>" "$<TARGET_FILE_DIR:${PROJECT_NAME}-test-rtty-plain-server-runtime>/../lib/rstream/")

add_test_target(${PROJECT_NAME}-test-rtty-websocket-server-runtime test_rtty_websocket_server_runtime.cpp)
target_include_directories(${PROJECT_NAME}-test-rtty-websocket-server-runtime PRIVATE ${RTTY_GENERATED_SOURCE_DIR})
target_link_libraries(${PROJECT_NAME}-test-rtty-websocket-server-runtime ${PROJECT_NAME}::rtty protobuf::libprotobuf)
add_dependencies(${PROJECT_NAME}-test-rtty-websocket-server-runtime ${PROJECT_NAME}-plugin-io-generic)
add_custom_command(TARGET ${PROJECT_NAME}-test-rtty-websocket-server-runtime POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${PROJECT_NAME}-test-rtty-websocket-server-runtime>/../lib/rstream"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:${PROJECT_NAME}-plugin-io-generic>" "$<TARGET_FILE_DIR:${PROJECT_NAME}-test-rtty-websocket-server-runtime>/../lib/rstream/")

add_test_target(${PROJECT_NAME}-test-rtty-plain-client-runtime test_rtty_plain_client_runtime.cpp)
target_include_directories(${PROJECT_NAME}-test-rtty-plain-client-runtime PRIVATE ${RTTY_GENERATED_SOURCE_DIR})
target_link_libraries(${PROJECT_NAME}-test-rtty-plain-client-runtime ${PROJECT_NAME}::rtty protobuf::libprotobuf)
add_dependencies(${PROJECT_NAME}-test-rtty-plain-client-runtime ${PROJECT_NAME}-plugin-io-generic)
add_custom_command(TARGET ${PROJECT_NAME}-test-rtty-plain-client-runtime POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${PROJECT_NAME}-test-rtty-plain-client-runtime>/../lib/rstream"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:${PROJECT_NAME}-plugin-io-generic>" "$<TARGET_FILE_DIR:${PROJECT_NAME}-test-rtty-plain-client-runtime>/../lib/rstream/")

add_test_target(${PROJECT_NAME}-test-rtty-websocket-client-runtime test_rtty_websocket_client_runtime.cpp)
target_include_directories(${PROJECT_NAME}-test-rtty-websocket-client-runtime PRIVATE ${RTTY_GENERATED_SOURCE_DIR})
target_link_libraries(${PROJECT_NAME}-test-rtty-websocket-client-runtime ${PROJECT_NAME}::rtty protobuf::libprotobuf)
add_dependencies(${PROJECT_NAME}-test-rtty-websocket-client-runtime ${PROJECT_NAME}-plugin-io-generic)
add_custom_command(TARGET ${PROJECT_NAME}-test-rtty-websocket-client-runtime POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${PROJECT_NAME}-test-rtty-websocket-client-runtime>/../lib/rstream"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:${PROJECT_NAME}-plugin-io-generic>" "$<TARGET_FILE_DIR:${PROJECT_NAME}-test-rtty-websocket-client-runtime>/../lib/rstream/")
