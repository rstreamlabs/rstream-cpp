# C++ SDK documentation

Start with [Build an application](000-getting-started.md) and the complete HTTP server in the [repository overview](../README.md). The SDK follows Boost.Asio’s execution model; the application owns its event loop and connection lifetimes.

| Integration guide | Use it to |
| --- | --- |
| [Build an application](000-getting-started.md) | Install a published SDK, link an application and connect to a project. |
| [Configuration](004-configuration.md) | Select credentials, a shared context and supported transport settings. |
| [Tunnel patterns](005-tunnel-patterns.md) | Publish stream services, dial privately and use different completion tokens. |
| [URI model](002-uri-model.md) | Parse and compose URI-based settings. |
| [Command-line tools](006-tools.md) | Use the companion native utilities. |

| Development guide | Scope |
| --- | --- |
| [Source build](007-building-from-source.md) | Compile this checkout and run its native tests. |
| [Contributing](../CONTRIBUTING.md) | API, style and change requirements. |
| [SDK engineering](001-sdk-engineering.md) | Asio contracts, platform support, linking and plugin topologies. |
| [Critical dependencies](003-critical-dependencies.md) | Dependency selection and upgrade constraints. |
