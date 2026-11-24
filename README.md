# rstream C++ SDK

rstream lets an application publish a service or reach a private service
through an outbound connection. The C++ SDK embeds that connection in native
services, gateways and devices. It provides Boost.Asio sockets and acceptors
that work with Boost.Beast, with the event loop and resource ownership in
the application.

| Task | Start here |
| --- | --- |
| Run a first application with the published SDK | [Build an application](docs/000-getting-started.md). |
| Add a tunnel to an existing Asio or Beast application | [Integrate HTTP, TLS or WebSocket with Beast](docs/004-http-tls-websocket.md). |
| Compile and test this checkout | [Build from source](docs/001-building-from-source.md). |
| Evaluate runtimes, deployment and protocol coverage | [Choose an SDK](https://rstream.io/sdks) and see [Editions and plans](https://rstream.io/docs/support/editions). |

<a name="keep-the-beast-handler"></a>

## Serve HTTP with Boost.Beast

The application creates a published HTTP/1.1 tunnel from an Asio coroutine.
The Engine returns the address to open in a browser.

```cpp
co_await client.async_connect(asio::use_awaitable);
rstrm::tunnel_properties properties{};
properties.m_protocol = rstrm::protocol::http;
properties.m_http_version = "http/1.1";
properties.m_publish = true;
auto tunnel = co_await client.async_create_tunnel(properties, asio::use_awaitable);
std::cout << rstrm::format_forwarding_address(tunnel.properties()).value() << std::endl;
```

For each socket accepted by the tunnel, the Beast handler reads the request
and returns a plain-text response.

```cpp
asio::awaitable<void> respond(rstrm::socket& socket)
{
  boost::beast::flat_buffer buffer;
  http::request<http::string_body> request;
  co_await http::async_read(socket, buffer, request, asio::use_awaitable);

  http::response<http::string_body> response{http::status::ok, request.version()};
  response.set(http::field::content_type, "text/plain");
  response.keep_alive(false);
  response.body() = "Hello from Boost.Beast through rstream\n";
  response.prepare_payload();
  co_await http::async_write(socket, response, asio::use_awaitable);
}
```

<details>
<summary>Build and run the standalone HTTP server</summary>

The runnable Boost.Beast project uses the published SDK. On macOS or Linux,
use a C++20 compiler, Conan 2, CMake 3.23 or newer, Ninja and Make.
Fetch the project and enter the directory containing its `conanfile.py` and `src/main.cpp`.

```sh
git clone --depth 1 https://github.com/rstreamlabs/rstream-examples.git
cd rstream-examples/cpp-beast-rstream-tunnel
```

If the default Conan profile is missing, run `conan profile detect`.
If `conan remote list` does not include the rstream repository, register it.

```sh
conan remote add rstream https://nexus.rstream.io/repository/conan
```

Build the application. The Makefile resolves the SDK with Conan and installs the executable in `out/bin/`.

```sh
make build
```

With a CLI context that permits HTTP publication, start the server from the same project directory.

```sh
./out/bin/cpp_beast_rstream_tunnel
```

Open the printed forwarding URL to receive `Hello from Boost.Beast through rstream`. Stop the process with Ctrl+C.

</details>

For an existing Beast service, the
[listener migration](docs/004-http-tls-websocket.md#move-a-beast-service-from-local-tcp-to-a-tunnel)
uses the same handler with TCP and rstream sockets.

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
