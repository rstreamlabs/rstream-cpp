# rstream C++ SDK

rstream lets an application publish a service or reach a private service
through an outbound connection. The C++ SDK embeds that connection in native
services, gateways and devices. It provides Boost.Asio sockets and acceptors
that work with Boost.Beast, with the event loop and resource ownership in
the application.

| What you want to do | Start here |
| --- | --- |
| Run a first application with the published SDK | [Build an application](docs/000-getting-started.md). |
| Add a tunnel to an existing Asio or Beast application | [Integrate HTTP, TLS or WebSocket with Beast](docs/005-http-tls-websocket.md). |
| Compile and test this checkout | [Build from source](docs/001-building-from-source.md). |
| Evaluate runtimes, deployment and protocol coverage | [Choose an SDK](https://rstream.io/docs/sdks) and see [Editions and plans](https://rstream.io/docs/support/editions). |

## Keep the Beast handler

The migration example uses one HTTP handler for both listeners. The local
version accepts a TCP socket. The tunneled version creates a published HTTP
tunnel and accepts an rstream socket on the same Asio executor.

```cpp
rstrm::socket socket(executor);
rstrm::endpoint peer;
co_await tunnel.async_accept(socket, peer, asio::use_awaitable);
asio::co_spawn(executor, example_http::serve(std::move(socket)), asio::detached);
```

[Run both versions](docs/005-http-tls-websocket.md#move-a-beast-service-from-local-tcp-to-a-tunnel)
and compare their responses. The example catalog also covers private dialing,
TLS passthrough and Beast WebSocket. [Compare handlers, futures and coroutines](docs/004-completion-models.md)
to see how each style owns the same connection and tunnel operation.

## Documentation

The [guide index](docs/README.md) leads from the first connection to tunnel
patterns, native runtime options and deployment. The SDK requires C++20 and
uses TLS to carry bytestream tunnels. The [native utilities](docs/012-command-line-tools.md)
are distributed separately as `rstream-utils`.

## Security and support

Report vulnerabilities through [Security](SECURITY.md). Use
[GitHub issues](https://github.com/rstreamlabs/rstream-cpp/issues) for
reproducible bugs and usage questions. See [Contributing](CONTRIBUTING.md) for
development practices. The SDK is licensed under [Apache-2.0](LICENSE).
