# Use and ship transport plugins

An `io::stream` resolves a URI to a registered transport. A direct
`io_rstrm::client` also uses the generic transport layer for its
connection to the Engine. In both cases, the plugin provides the transport
and the application owns the Asio executor and its sessions.

## Choose the transports

| Plugin | What it provides |
| --- | --- |
| `io-generic` | TCP, Unix sockets and serial streams on supported platforms. It also supplies the TCP connection to the Engine used by the direct C++ API. |
| `io-rstrm` | The `rstrm://` resolver, acceptor and socket for URI-selected tunnels. |

TLS is a layer on a stream. For example, `tcp://service.example:443?ssl`
selects TCP and enables TLS for that connection. The [URI reference](011-uri-reference.md#tls-stream-options)
covers its certificate options. A tunnel's public TLS settings apply to a
different connection, as shown in the [TLS guide](beast/000-tls.md).

[Link the application target](000-getting-started.md#link-an-existing-cmake-target)
and select the plugin targets that supply its transports. The
[URI HTTP pair](003-tunnel-patterns.md#uri-configured-listeners)
uses both plugins to run the same HTTP exchange over TCP and rstream.

## Choose how the application ships them

Library linkage and plugin registration are separate decisions. With Conan,
select them with `rstream/*:shared` and `rstream/*:static_plugins`.
In a direct CMake build those choices are `BUILD_SHARED_LIBS` and
`ENABLE_STATIC_PLUGINS`. The Conan recipe selects shared Protobuf,
Abseil and yaml-cpp whenever SDK libraries or plugins are dynamic.
Dynamic plugins require `static_libstdcxx=False`.

| SDK libraries | Plugins | Runtime layout |
| --- | --- | --- |
| Static | Static | The executable carries the built-in providers. Ship any other shared dependencies used by the application. |
| Shared | Static | Ship the SDK shared libraries and their dependencies. The application links the built-in providers through the SDK target. |
| Static | Dynamic | Ship the modules and their shared dependencies alongside the executable. The rstream module uses a shared Protobuf runtime even when the SDK itself is static. |
| Shared | Dynamic | Ship the SDK shared libraries, modules and their transitive runtime dependencies together. |

For static registration, link `rstream::rstream` into the final application
so that the SDK's built-in registration is retained. For dynamic registration,
`rstream_enable_runtime_plugins` copies module targets next to the
build-tree executable, under `../lib/rstream`. This prepares a
build-tree run. For deployment, preserve the configured module directory
and include the libraries needed by each module.

## Preserve the install layout

Use the standard installation layout with `CMAKE_INSTALL_LIBDIR=lib`.
The build helper and SDK runtime paths use `lib`, while the loader's
install directory is configurable. Keep executable, SDK and plugins
from the same build. The loader starts from the executable's location.

## Validate the application package

The [dynamic install walkthrough](packaging/000-dynamic-install.md)
prepares those directories for an external CMake application. It includes
the SDK's shared dependencies and tests both transports from the staged
installation.

## Verify registration and loading

A standard Debug source build includes `rstream-inspect`. Check the
default search path from the executable as well as any explicit module
directory you intend to deploy.

```sh
rstream-inspect plugins
rstream-inspect plugins -v --search-path="$PWD/stage/lib/rstream"
```

The output should include `io-generic`, `io-rstrm` and the
`io.stream.tcp` and `io.stream.rstrm` elements for the
two-transport application. For dynamic plugins, check the type and module
location as well. Static providers appear even with an empty module
directory. The explicit search path above belongs to the inspector.
Use the actual application's default layout for the final check.

| Result | What to check |
| --- | --- |
| `plugin not found` for a URI | Check its scheme and the expected `io.stream.*` element. For dynamic builds, check the default directory from the executable and run the inspector with `-v`. |
| Module present with a load error | Check architecture and all of the module's shared dependencies on the target. Use SDK libraries and modules produced together. |
| Elements registered, connection fails | Check DNS, Engine context and TLS verification. Registration only establishes that the transport is available. |

## Add a transport to the SDK

A transport provider supplies a resolver, an acceptor and a socket through
the [stream concept](../lib/rstream/io/detail/stream/stream.hpp). Start
with the [TCP provider](../plugin/io-generic/tcp/stream.hpp) for the
shape of those operations. Choose a unique scheme. Register the
provider with `rstream::io::make_stream<Provider>()`, which exposes it
as `io.stream.<scheme>` to the generic resolver.

The [generic plugin entry point](../plugin/io-generic/plugin.cpp) shows
where to put that element in a descriptor. Use `RSTREAM_PLUGIN_EXPORT`
for a dynamic module. Built-in modules use `RSTREAM_PLUGIN_STATIC_DEFINE`
and the [static registration target](../lib/rstream/static-plugins/static-plugins.cpp)
keeps them reachable in the linked application. These extension points
use C++ types from the SDK. Build the provider and application against
matching headers and libraries.

Verify that the inspector lists the element, then resolve the scheme and
exercise connect, accept, read, write and cancellation through
`io::stream`. An inspector entry confirms discovery. The stream exchange
confirms that the provider implements the operations the application needs.

For SDK package maintainers, the [Conan package matrix](../conan/README.md#conan-package-matrix)
exercises the four linkage choices with external consumers. The
[plugin API](../lib/rstream/core/plugin.hpp) defines the descriptor and
entry-point macros.
