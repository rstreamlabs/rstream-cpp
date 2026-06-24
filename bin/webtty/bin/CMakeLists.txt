# See LICENSE file in the project root for license information.

# process subdirectories

add_subdirectory(client)
add_subdirectory(server)

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
