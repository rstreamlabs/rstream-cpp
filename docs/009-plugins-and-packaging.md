# Plugins and packaging

rstream libraries and transport plugins have independent linkage choices. This
lets an application choose embedded plugin registration or runtime modules.

| SDK libraries | Plugins | Deployment                                                       |
| ------------- | ------- | ---------------------------------------------------------------- |
| Static        | Static  | Keep plugin registration reachable when linking the executable.  |
| Shared        | Static  | Ship the SDK shared libraries with plugin registration retained. |
| Static        | Dynamic | Ship the executable and selected plugin modules.                 |
| Shared        | Dynamic | Ship the SDK shared libraries and selected plugin modules.       |

`BUILD_SHARED_LIBS` controls library linkage. `ENABLE_STATIC_PLUGINS` controls
plugin registration/loading. Link the public `rstream::rstream` target and use
`rstream_enable_runtime_plugins` for the selected plugins. The
[application guide](000-getting-started.md#link-an-application) gives complete
Conan and CMake files.

## Preserve the install layout

Dynamic modules live under `lib/rstream`. Preserve the installation layout and
the required shared libraries when distributing an application. The plugin
loader discovers those modules at runtime. In a Debug tools build,
`rstream-inspect plugins --help` describes registry/search-path inspection.

URI schemes select the corresponding transport plugins; the
[URI reference](011-uri-reference.md) explains their parameter namespaces.
Keep engine credentials in the trusted application configuration when accepting
URI input from a user.

## Validate the application package

Run an external application against the installed package, including its
selected plugins. The [engineering package matrix](014-sdk-engineering.md#conan-package-matrix)
defines the contributor checks for each topology. For a different CPU, libc or
operating system, continue with [cross-compilation](010-cross-compilation.md).
