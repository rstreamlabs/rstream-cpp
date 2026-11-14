# C++ SDK documentation

Start with [Build and integrate](000-getting-started.md) and the complete HTTP server in the [repository overview](../README.md). The SDK follows Boost.Asio’s execution model; the application owns its event loop and connection lifetimes.

| Integration guide | Use it to |
| --- | --- |
| [Getting started](000-getting-started.md) | Build the package, link an application and connect to a project. |
| [Configuration](004-configuration.md) | Select credentials, a shared context and supported transport settings. |
| [Tunnel patterns](005-tunnel-patterns.md) | Publish stream services, dial privately and use different completion tokens. |
| [URI model](002-uri-model.md) | Parse and compose URI-based settings. |
| [Command-line tools](006-tools.md) | Use the companion native utilities. |

| Development guide | Scope |
| --- | --- |
| [Contributing](../CONTRIBUTING.md) | Local setup, style and checks. |
| [SDK engineering](001-sdk-engineering.md) | Asio contracts, platform support, linking and plugin topologies. |
| [Critical dependencies](003-critical-dependencies.md) | Dependency selection and upgrade constraints. |
