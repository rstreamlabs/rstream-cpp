# C++ SDK guides

The SDK brings published and private bytestream tunnels into applications
that use Boost.Asio. Choose how to run your first application, then follow
the guides for the protocols and runtime choices it needs. If a service
already listens locally and you want to keep its code as it is,
[forward it with the native tool](tools/000-forwarding.md).

## Get started

| Task | Guide |
| --- | --- |
| Build a first application with the published SDK | [Build an application](000-getting-started.md). |
| Compile and test this checkout | [Build from source](001-building-from-source.md). |
| Connect to a project or a direct Engine | [Configure the SDK](002-configuration.md). |

## Integrate a service

| Guide | Decision |
| --- | --- |
| [Tunnel patterns](003-tunnel-patterns.md) | Choose published or private access, then use the [direct client](003-tunnel-patterns.md#use-a-direct-client-and-tunnel), [typed acceptor](003-tunnel-patterns.md#use-a-typed-rstream-acceptor) or [URI interface](003-tunnel-patterns.md#uri-configured-listeners). |
| [HTTP, TLS and WebSocket with Beast](004-http-tls-websocket.md) | Migrate a TCP listener, own the TLS handshake or serve WebSocket. |
| [Native example catalog](../example/io-rstrm/README.md) | Find complete source programs, build targets and expected results. |

## Native runtime choices

| Guide | Decision |
| --- | --- |
| [Completion styles](005-completion-models.md) | Compare handlers, futures and coroutines on one operation. |
| [Execution and object lifetime](006-execution-and-lifetime.md) | Own the event loop, strands, shared handles and buffers. |
| [Memory allocation](007-memory-allocation.md) | Supply an SDK allocator or an Asio associated allocator. |
| [Cancellation and recovery](008-cancellation-and-recovery.md) | Bound operations, drain completions and recover a service. |

## Deploy and operate

| Guide | Task |
| --- | --- |
| [Plugins and packaging](009-plugins-and-packaging.md) | Ship the libraries and transport plugins the application uses. |
| [Cross-compilation](010-cross-compilation.md) | Build for a device's toolchain and ABI. |
| [URI reference](011-uri-reference.md) | Look up transport and tunnel options for a configuration-driven listener. |
| [Native command-line tools](012-command-line-tools.md) | Install `rstream-utils`, then [forward a service](tools/000-forwarding.md), [relay a stream](tools/001-streams.md) or [measure a connection](tools/002-measurement.md). |
| [WebTTY](013-webtty.md) | Run a native terminal session and choose how its endpoints authenticate. |

[Contributing](../CONTRIBUTING.md) covers SDK development, build validation and
the engineering requirements for changes to the library.
