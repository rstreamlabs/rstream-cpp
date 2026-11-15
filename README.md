# rstream C++ SDK

rstream connects services and devices through an outbound connection to an edge, making them reachable without opening inbound ports on their hosts. Applications can expose a forwarding address or connect privately through an authenticated client.

The C++ SDK embeds that connection model in native services, gateways and devices. It follows Boost.Asio's asynchronous model, so executors, cancellation and completion tokens fit existing Asio and Boost.Beast applications. The application owns its event loop and connection lifetimes.

The [SDK guide](https://rstream.io/docs/sdks) compares language runtimes and protocol coverage.

## Connection model

An rstream client establishes outbound connectivity to the edge. Published tunnels provide a forwarding address for ordinary clients; private tunnels are reached by name or ID through an authenticated rstream client. The edge routes connections and enforces the configured access policy.

| Area | C++ support |
| --- | --- |
| Runtime | Asynchronous bytestream tunnels over a TLS engine connection. |
| Application integration | Asio-compatible sockets, callbacks, futures and C++20 coroutines. |
| Services | Published HTTP and stream protocols; private stream accept and dial. |
| Configuration | Shared CLI contexts, environment variables and URI-based options. |
| Platforms | Linux, macOS and Windows, with build profiles for native and constrained systems. |

Datagram tunnels and QUIC runtime transport are outside the current implementation. A shared configuration that explicitly requests an unsupported transport is rejected.

## Get started

Choose the path matching the work:

| Goal | Guide |
| --- | --- |
| Use the published SDK in an application | [Build an application](docs/000-getting-started.md): Conan dependencies, CMake targets and a first connection. |
| Compile or modify this checkout | [Build the SDK from source](docs/001-building-from-source.md): toolchain, native configuration, build output and tests. |
| Use the companion native tools | [Command-line tools](docs/005-tools.md), distributed separately as `rstream-utils`. |

The SDK requires C++20. The HTTP example demonstrates the application model; its CMake and Conan files are in the application guide.

## Serve HTTP with Boost.Beast

The example creates a published HTTP tunnel and serves one request per accepted connection. It uses the selected CLI context and prints the forwarding address.

Start with the [local TCP → rstream migration](docs/008-http-tls-websocket.md#move-a-beast-service-from-local-tcp-to-a-tunnel): two executable entry points share the same Beast handler, with tested request/response behavior.

The complete [HTTP server](example/io-rstrm/http-server.cpp) uses
`http::async_read` and `http::async_write` directly on an rstream socket. Its
[application build files](docs/000-getting-started.md#link-an-application) select
the public CMake targets and runtime plugins. The example serves HTTP/1.1 in
process; the edge provides the public HTTPS endpoint.

The application supplies its `io_context` and executors, chooses its threads,
and keeps clients, sockets, handlers and buffers alive. The same asynchronous
operations accept callbacks, futures or `co_await`; executor, allocator and
cancellation associations follow the handler. See the [integration choices and
complete programs](docs/003-tunnel-patterns.md) for the ownership and shutdown
that each style requires.

C++ is useful when the connection belongs inside an existing native Asio
service or gateway. It requires C++20, Boost and a native build/package setup;
the caller also owns asynchronous lifetimes. The Go SDK and CLI provide the
reference runtime and broader protocol/tooling coverage. Choose C++ for that
native integration with its documented transport limits; no throughput or
footprint advantage is implied without measurements.

## Documentation

| Task | Guide |
| --- | --- |
| Build and run a first application | [Getting started](docs/000-getting-started.md) |
| Set credentials, contexts or transport options | [Configuration](docs/002-configuration.md) |
| Dial privately or choose another completion style | [Tunnel patterns](docs/003-tunnel-patterns.md) |
| Configure a URI | [URI model](docs/004-uri-model.md) |
| Use the packaged utilities | [Command-line tools](docs/005-tools.md) |
| Understand platform, plugin and packaging constraints | [SDK engineering](docs/006-sdk-engineering.md) |
| Develop the SDK | [Source build](docs/001-building-from-source.md), [Contributing](CONTRIBUTING.md) and [documentation index](docs/README.md) |

## Security and support

Use [Security](SECURITY.md) for private vulnerability reporting. Usage questions and reproducible bugs can be reported in [GitHub issues](https://github.com/rstreamlabs/rstream-cpp/issues); remove credentials and customer details first. Licensed under [Apache-2.0](LICENSE).
