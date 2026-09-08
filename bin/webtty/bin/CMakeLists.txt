# See LICENSE file in the project root for license information.

# process subdirectories

add_subdirectory(client)
add_subdirectory(server)

if(ENABLE_TESTING)
  find_package(Python3 COMPONENTS Interpreter REQUIRED)
  math(EXPR WEBTTY_STARTUP_TIMEOUT "10 * ${RSTREAM_TEST_TIMEOUT_SCALE}")
  foreach(WEBTTY_BINARY client server)
    foreach(WEBTTY_OPTION help version)
      add_test(NAME ${PROJECT_NAME}-test-webtty-${WEBTTY_BINARY}-${WEBTTY_OPTION}
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_cli_startup.py
          $<TARGET_FILE:${PROJECT_NAME}-webtty-${WEBTTY_BINARY}> --${WEBTTY_OPTION} ${WEBTTY_STARTUP_TIMEOUT})
      rstream_configure_test(${PROJECT_NAME}-test-webtty-${WEBTTY_BINARY}-${WEBTTY_OPTION})
    endforeach()
    set_property(GLOBAL APPEND PROPERTY RSTREAM_TEST_TARGETS ${PROJECT_NAME}-webtty-${WEBTTY_BINARY})
  endforeach()
endif()

if(UNIX)
  foreach(WEBTTY_BINARY client server)
    install(CODE "
      set(WEBTTY_COMPAT_BINDIR \"\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}\")
      set(WEBTTY_COMPAT_LINK \"\$ENV{DESTDIR}\${WEBTTY_COMPAT_BINDIR}/${PROJECT_NAME}-rtty-${WEBTTY_BINARY}\")
      execute_process(COMMAND \"${CMAKE_COMMAND}\" -E rm -f \"\${WEBTTY_COMPAT_LINK}\")
      execute_process(COMMAND \"${CMAKE_COMMAND}\" -E create_symlink \"${PROJECT_NAME}-webtty-${WEBTTY_BINARY}\" \"\${WEBTTY_COMPAT_LINK}\")
    ")
  endforeach()
endif()
