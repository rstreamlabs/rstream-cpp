# C++ SDK guides

Start with a first application or build this checkout, then configure the
connection. The application owns its Asio executor and the lifecycle of its
clients, tunnels and sockets.

| Start                                  | Guide                                                |
| -------------------------------------- | ---------------------------------------------------- |
| Add rstream to an application          | [000 Build an application](000-getting-started.md)   |
| Compile this checkout and its examples | [001 Build from source](001-building-from-source.md) |
| Select engine, identity and TLS trust  | [002 Configuration](002-configuration.md)            |

## Integrate with the application

| Guide                                                    | Question answered                                                              |
| -------------------------------------------------------- | ------------------------------------------------------------------------------ |
| [003 Integration model](003-integration-model.md)        | Direct API, acceptor or URI streams? Private dial or public listener?          |
| [004 Completion models](004-completion-models.md)        | How does the same operation look with handlers, futures and coroutines?        |
| [005 HTTP, TLS and WebSocket](005-http-tls-websocket.md) | How does an existing Asio/Beast service use a tunnel and preserve its handler? |

## Native runtime choices

Use the reference for the decision your application needs:

| Guide                                                             | Topic                                                             |
| ----------------------------------------------------------------- | ----------------------------------------------------------------- |
| [006 Execution and lifetime](006-execution-and-lifetime.md)       | Event loops, strands, shared handles and buffers.                 |
| [007 Memory allocation](007-memory-allocation.md)                 | SDK object allocators, associated allocators and backing storage. |
| [008 Cancellation and recovery](008-cancellation-and-recovery.md) | Deadlines, cancellation completion, shutdown and reconnection.    |
| [009 Plugins and packaging](009-plugins-and-packaging.md)         | Library linkage, plugin registration and installed layout.        |
| [010 Cross-compilation](010-cross-compilation.md)                 | Build/host profiles, target dependencies and device validation.   |
| [011 URI reference](011-uri-reference.md)                         | Transport namespaces, parameters and encoding.                    |
| [012 Command-line tools](012-command-line-tools.md)               | Packaged native utilities.                                        |
| [013 WebTTY](013-webtty.md)                                       | Native terminal client/server, identity and process ownership.    |

## Develop the SDK

[Contributing](../CONTRIBUTING.md) gives the local workflow. The
[014 engineering contract](014-sdk-engineering.md) defines Asio, security,
platform and package requirements. [015 Dependency constraints](015-dependency-constraints.md)
covers provider and dependency validation.
