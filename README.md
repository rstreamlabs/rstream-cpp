# rstream C++ SDK

rstream lets an application publish a service or reach a private service
through an outbound connection. The C++ SDK embeds that connection in native
services, gateways and devices. It provides Boost.Asio sockets and acceptors
that work with Boost.Beast, with the event loop and resource ownership in
the application.

| What you want to do | Start here |
| --- | --- |
| Run a first application with the published SDK | [Build an application](docs/000-getting-started.md). |
| Add a tunnel to an existing Asio or Beast application | [Integrate HTTP, TLS or WebSocket with Beast](docs/004-http-tls-websocket.md). |
| Compile and test this checkout | [Build from source](docs/001-building-from-source.md). |
| Evaluate runtimes, deployment and protocol coverage | [Choose an SDK](https://rstream.io/sdks) and see [Editions and plans](https://rstream.io/docs/support/editions). |

<a id="keep-the-beast-handler"></a>

## Serve HTTP with Boost.Beast

With a connected `rstrm::client`, this coroutine opens an HTTP tunnel
and accepts sockets on the application's existing Asio executor.

```cpp
rstrm::tunnel_properties properties;
properties.m_publish = true;
properties.m_protocol = rstrm::protocol::http;
properties.m_http_version = std::string("http/1.1");
auto tunnel = co_await client.async_create_tunnel(properties, asio::use_awaitable);
auto forwarding = rstrm::format_forwarding_address(tunnel.properties());
if (forwarding)
{
  std::cout << "Forwarding address: " << forwarding.value() << std::endl;
}
for (;;)
{
  rstrm::socket socket(executor);
  rstrm::endpoint peer;
  co_await tunnel.async_accept(socket, peer, asio::use_awaitable);
  asio::co_spawn(executor, serve_session(std::move(socket)), asio::detached);
}
```

In `serve_session`, Beast reads the request and writes the hostname as
the response body on that same socket.

```cpp
beast::flat_buffer buffer;
http::request<http::string_body> request;
co_await http::async_read(socket, buffer, request, asio::use_awaitable);
http::response<http::string_body> response{http::status::ok, request.version()};
response.set(http::field::content_type, "text/plain; charset=utf-8");
response.keep_alive(false);
response.body() = asio::ip::host_name();
response.prepare_payload();
co_await http::async_write(socket, response, asio::use_awaitable);
```

[Build and run this server](docs/000-getting-started.md) with the published
SDK. To adapt an existing TCP server, the [Beast migration guide](docs/004-http-tls-websocket.md#move-a-beast-service-from-local-tcp-to-a-tunnel)
shows how both listeners can call the same HTTP handler.

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
