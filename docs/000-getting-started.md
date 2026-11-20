# Build an application with the C++ SDK

Run a small Boost.Beast server using the SDK distributed through Conan. The
server opens a published HTTP tunnel, responds with the machine's hostname,
and closes its tunnel when you stop the program. It uses the application's
Asio event loop.

## Get the complete example

Open the C++ tab in the [SDK code samples](https://rstream.io/sdks#sdk-code-samples) and download
the archive. It contains the source, Conan recipe and CMake target together.
Use the archive's README for platform prerequisites and build commands.
Conan selects the published package pinned in that recipe.

Use an installed [rstream CLI](https://rstream.io/docs/getting-started/installation)
and a hosted project that permits HTTP publication, or a [direct Community
Edition Engine](002-configuration.md#connect-to-a-direct-engine). The CLI
sets up the development context. The example itself runs as a native C++
process. Review the [HTTP access
policy](https://rstream.io/docs/tunnels/http-tunnels/authentication) before
publishing a service that needs to restrict callers.

## Verify the application

After the build, start the binary and open the HTTPS address printed as
`Forwarding address`. Expect HTTP 200 and your machine's hostname. With a
private Engine CA, verify the printed hostname with that CA in your HTTP
client. The SDK's Engine trust and the HTTP caller's trust are configured
independently. Use the [required caller credential](https://rstream.io/docs/tunnels/http-tunnels/authentication#token-authentication)
if the project requires it. Stop the binary with Ctrl+C.

To run against different credentials or an Engine with a private CA, see
[Configure the SDK](002-configuration.md). For programs from this checkout,
[build from source](001-building-from-source.md).

## Link an existing CMake target

Use the same Conan SDK requirement as the example, or provide an installed
SDK and its public dependencies through the application's CMake prefix.

```cmake
find_package(rstream CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE rstream::rstream)
rstream_enable_runtime_plugins(
  my_app
  rstream::plugin::io-generic
  rstream::plugin::io-rstrm)
```

The target propagates C++20 and the SDK's public dependencies. The helper
selects the transport plugins used by the application. See [plugins and
packaging](009-plugins-and-packaging.md) when distributing the binary, and
[the Beast migration](005-http-tls-websocket.md#move-a-beast-service-from-local-tcp-to-a-tunnel)
to adapt an existing listener.
