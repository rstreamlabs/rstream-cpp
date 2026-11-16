# rstream C++ SDK

rstream connects services and devices through an outbound connection to an edge, making them reachable without opening inbound ports on their hosts. Applications can expose a forwarding address or connect privately through an authenticated client.

The C++ SDK embeds that connection model in native services, gateways and devices. It follows Boost.Asio's asynchronous model, so executors, cancellation and completion tokens fit existing Asio and Boost.Beast applications. The application owns its event loop and connection lifetimes.

The [SDK guide](https://rstream.io/docs/sdks) compares language runtimes and protocol coverage.

## Connection model

An rstream client establishes outbound connectivity to the edge. Published tunnels provide a forwarding address for ordinary clients; private tunnels are reached by name or ID through an authenticated rstream client. The edge routes connections and enforces the configured access policy.

| Area                    | C++ support                                                                       |
| ----------------------- | --------------------------------------------------------------------------------- |
| Runtime                 | Asynchronous bytestream tunnels over a TLS engine connection.                     |
| Application integration | Asio-compatible sockets, callbacks, futures and C++20 coroutines.                 |
| Services                | Published HTTP and stream protocols; private stream accept and dial.              |
| Configuration           | Shared CLI contexts, environment variables and URI-based options.                 |
| Platforms               | Linux, macOS and Windows, with build profiles for native and constrained systems. |

Datagram tunnels and QUIC runtime transport are outside the current implementation. A shared configuration that explicitly requests an unsupported transport is rejected.

## Get started

Choose the path matching the work:

| Goal                                    | Guide                                                                                                                   |
| --------------------------------------- | ----------------------------------------------------------------------------------------------------------------------- |
| Use the published SDK in an application | [Build an application](docs/000-getting-started.md): Conan dependencies, CMake targets and a first connection.          |
| Compile or modify this checkout         | [Build the SDK from source](docs/001-building-from-source.md): toolchain, native configuration, build output and tests. |
| Use the companion native tools          | [Command-line tools](docs/012-command-line-tools.md), distributed separately as `rstream-utils`.                        |

The SDK requires C++20. The HTTP example demonstrates the application model; its CMake and Conan files are in the application guide.

## Serve HTTP with Boost.Beast

An existing Beast handler can serve an ordinary TCP socket or an rstream
socket. These excerpts from the complete migration example change the
listener while keeping the same `example_http::serve` handler.

With a local Asio acceptor:

```cpp
auto socket = co_await acceptor.async_accept(asio::use_awaitable);
asio::co_spawn(executor, example_http::serve(std::move(socket)), asio::detached);
```

With an authenticated rstream client and its HTTP tunnel:

```cpp
rstrm::socket socket(executor);
rstrm::endpoint peer;
co_await tunnel.async_accept(socket, peer, asio::use_awaitable);
asio::co_spawn(executor, example_http::serve(std::move(socket)), asio::detached);
```

The [migration walkthrough](docs/005-http-tls-websocket.md#move-a-beast-service-from-local-tcp-to-a-tunnel)
provides both executable entry points, their shared handler, client/tunnel
setup and build files. The application serves HTTP/1.1 in process; the edge
provides the public HTTPS endpoint. The same guide demonstrates
application-owned TLS and a Beast WebSocket server.

Choose callbacks, futures or coroutines to match the application. The
[equivalent integration programs](docs/004-completion-models.md) show event-loop
ownership, errors and cleanup for each style. The [documentation index](docs/README.md#native-runtime-choices) leads to focused
guides for execution, allocation, cancellation and native packaging.

## Documentation

| Task                                                  | Guide                                                                                                                       |
| ----------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------- |
| Build and run a first application                     | [Getting started](docs/000-getting-started.md)                                                                              |
| Set credentials, contexts or transport options        | [Configuration](docs/002-configuration.md)                                                                                  |
| Choose the runtime interface or private dial          | [Integration model](docs/003-integration-model.md)                                                                          |
| Compare handlers, futures and coroutines              | [Completion models](docs/004-completion-models.md)                                                                          |
| Configure a URI                                       | [URI reference](docs/011-uri-reference.md)                                                                                  |
| Use the packaged utilities                            | [Command-line tools](docs/012-command-line-tools.md)                                                                        |
| Understand platform, plugin and packaging constraints | [SDK engineering](docs/014-sdk-engineering.md)                                                                              |
| Develop the SDK                                       | [Source build](docs/001-building-from-source.md), [Contributing](CONTRIBUTING.md) and [documentation index](docs/README.md) |

## Security and support

Use [Security](SECURITY.md) for private vulnerability reporting. Usage questions and reproducible bugs can be reported in [GitHub issues](https://github.com/rstreamlabs/rstream-cpp/issues); remove credentials and customer details first. Licensed under [Apache-2.0](LICENSE).
