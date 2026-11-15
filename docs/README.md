# C++ SDK documentation

Choose [a published-package application](000-getting-started.md) when adding
rstream to a service. Choose [the source build](001-building-from-source.md)
when trying the checkout's examples or modifying the SDK. Both use the
application's Asio event loop; source compilation and local tests do not need a
project or credentials.

Continue with configuration, then choose how the application will integrate:

| Step | Guide | Result |
| --- | --- | --- |
| 1 | [Build an application](000-getting-started.md) | Conan/CMake files and a first HTTP connection using the published SDK. |
| 2 | [Build this checkout](001-building-from-source.md) | A native build tree, maintained example targets and local tests. |
| 3 | [Configuration](002-configuration.md) | A selected context or provisioned engine/identity, with verified TLS. |
| 4 | [Integration and tunnel patterns](003-tunnel-patterns.md) | A choice between direct API, acceptor and URI streams; callback/future/coroutine programs and private dial. |
| 5 | [HTTP, TLS and WebSocket](008-http-tls-websocket.md) | The handler protocol, certificate owner and Beast integration. |
| Next | [Advanced native integration](009-native-integration.md) | Associated executors/allocators, cancellation, plugin deployment, cross builds and WebTTY. |
| Reference | [URI model](004-uri-model.md) | Parameter namespaces, encoding and examples for the selected transport. |
| Tools | [Command-line tools](005-tools.md) | Native utilities and their own authentication/transport workflows. |

SDK development has additional obligations. [Contributing](../CONTRIBUTING.md)
explains the change workflow; [SDK engineering](006-sdk-engineering.md) preserves
Asio, platform, plugin, packaging and security contracts.
[Critical dependencies](007-critical-dependencies.md) defines provider/toolchain
qualification. These acceptance gates do not disappear when the first local
program builds successfully.
