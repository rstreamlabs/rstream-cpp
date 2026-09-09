# See LICENSE file in the project root for license information.

# process tests

add_test_target(${PROJECT_NAME}-test-core-executor-binder test_core_executor_binder.cpp ${PROJECT_NAME}::core)
add_test_target(${PROJECT_NAME}-test-core-error test_core_error.cpp ${PROJECT_NAME}::core)
add_test_target(${PROJECT_NAME}-test-core-log test_core_log.cpp ${PROJECT_NAME}::core)
if(WIN32)
  add_test_target(${PROJECT_NAME}-test-core-windows-cancel-io test_core_windows_cancel_io.cpp ${PROJECT_NAME}::core)
  add_test_target(${PROJECT_NAME}-test-core-windows-blocking-handle test_core_windows_blocking_handle.cpp ${PROJECT_NAME}::core)
  if(RSTREAM_TEST_WINDOWS_PIPE_ASAN)
    if(NOT MSVC OR NOT CMAKE_BUILD_TYPE MATCHES "^(Release|RelWithDebInfo)$")
      message(FATAL_ERROR "Windows pipe ASan tests require a native MSVC Release or RelWithDebInfo build.")
    endif()
    add_test_target(
      ${PROJECT_NAME}-test-core-windows-blocking-handle-asan
      "test_core_windows_blocking_handle.cpp;${PROJECT_SOURCE_DIR}/lib/rstream/core/windows/blocking_handle.cpp"
      ws2_32 mswsock)
    target_include_directories(${PROJECT_NAME}-test-core-windows-blocking-handle-asan PRIVATE ${PROJECT_SOURCE_DIR}/lib)
    target_compile_features(${PROJECT_NAME}-test-core-windows-blocking-handle-asan PRIVATE cxx_std_20)
    target_compile_options(${PROJECT_NAME}-test-core-windows-blocking-handle-asan PRIVATE /fsanitize=address /Zi)
    target_link_options(${PROJECT_NAME}-test-core-windows-blocking-handle-asan PRIVATE /INFERASANLIBS /DEBUG /INCREMENTAL:NO)
  endif()
endif()
add_test_target(${PROJECT_NAME}-test-core-memory-buffer test_core_memory_buffer.cpp ${PROJECT_NAME}::core)
add_test_target(${PROJECT_NAME}-test-core-protobuf test_core_protobuf.cpp ${PROJECT_NAME}::core)
add_test_target(${PROJECT_NAME}-test-core-exception-system test_core_exception_system.cpp ${PROJECT_NAME}::core)
add_library(${PROJECT_NAME}-test-plugin-without-entrypoint MODULE test_plugin_without_entrypoint.cpp)
set_target_properties(
  ${PROJECT_NAME}-test-plugin-without-entrypoint
  PROPERTIES
    PREFIX ""
    OUTPUT_NAME "${PROJECT_NAME}-test-plugin-without-entrypoint")
add_test_target(${PROJECT_NAME}-test-core-plugin-loader test_core_plugin_loader.cpp ${PROJECT_NAME}::core)
target_compile_definitions(
  ${PROJECT_NAME}-test-core-plugin-loader
  PRIVATE RSTREAM_TEST_PLUGIN_WITHOUT_ENTRYPOINT_PATH="$<TARGET_FILE:${PROJECT_NAME}-test-plugin-without-entrypoint>")
add_dependencies(${PROJECT_NAME}-test-core-plugin-loader ${PROJECT_NAME}-test-plugin-without-entrypoint)
add_test_target(${PROJECT_NAME}-test-core-plugin-version test_core_plugin_version.cpp ${PROJECT_NAME}::io)
if(NOT ENABLE_STATIC_PLUGINS)
  target_compile_definitions(${PROJECT_NAME}-test-core-plugin-version PRIVATE RSTREAM_TEST_DYNAMIC_PLUGINS)
  rstream_enable_runtime_plugins(
    ${PROJECT_NAME}-test-core-plugin-version
    ${PROJECT_NAME}-plugin-io-generic
    ${PROJECT_NAME}-plugin-io-rstrm)
endif()
add_test_target(${PROJECT_NAME}-test-core-plugin-registry test_core_plugin_registry.cpp ${PROJECT_NAME}::core)
add_test_target(${PROJECT_NAME}-test-core-random test_core_random.cpp ${PROJECT_NAME}::core)
