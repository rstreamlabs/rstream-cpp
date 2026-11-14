# rstream C++ SDK

Embed rstream connectivity in native services, gateways and devices. The C++ SDK follows Boost.Asio’s asynchronous model, with executors, cancellation and completion tokens that fit existing Asio and Boost.Beast applications.

It focuses on native performance and predictable resource ownership. The [Go SDK](https://github.com/rstreamlabs/rstream-go) is the reference implementation and provides broader protocol coverage.

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

Follow [Build and integrate](docs/000-getting-started.md) to build the SDK, link `rstream::rstream` in a CMake application and select a project context. The SDK requires C++20. Its companion command-line tools are distributed separately as `rstream-utils`.

## Serve HTTP with Boost.Beast

This complete program creates a published HTTP tunnel and serves one request per accepted connection. It uses the selected CLI context and prints the forwarding address.

```cpp
#include <csignal>
#include <iostream>

#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>

#include <rstream/core/exception.hpp>
#include <rstream/io-rstrm/client.hpp>
#include <rstream/io-rstrm/io-rstrm.hpp>
#include <rstream/io-rstrm/socket.hpp>

namespace asio  = boost::asio;
namespace http  = boost::beast::http;
namespace rstrm = rstream::io_rstrm;

asio::awaitable<void> session(rstrm::socket socket)
{
  try {
    http::request<http::string_body> req;
    boost::beast::flat_buffer buffer;
    co_await http::async_read(socket, buffer, req, asio::use_awaitable);
    http::response<http::string_body> res{http::status::ok, req.version()};
    res.set(http::field::content_type, "text/plain");
    res.keep_alive(false);
    res.body() = "Hello from rstream-cpp";
    res.prepare_payload();
    co_await http::async_write(socket, res, asio::use_awaitable);
    boost::system::error_code ec;
    socket.close(ec);
  }
  catch (const std::exception& e) {
    std::cerr << "session error: " << e.what() << std::endl;
  }
}

asio::awaitable<void> listener()
{
  auto executor = co_await asio::this_coro::executor;
  rstrm::client client(executor);
  co_await client.async_connect(asio::use_awaitable);
  rstrm::tunnel_properties properties = {.m_publish = true, .m_protocol = rstrm::protocol::http};
  auto tunnel                         = co_await client.async_create_tunnel(properties, asio::use_awaitable);
  auto forwarding                     = rstrm::format_forwarding_address(tunnel.properties());
  if (forwarding) {
    std::cout << "Forwarding address: " << forwarding.value() << std::endl;
  }
  while (true) {
    rstrm::socket socket(executor);
    rstrm::endpoint peer;
    co_await tunnel.async_accept(socket, peer, asio::use_awaitable);
    asio::co_spawn(executor, session(std::move(socket)), asio::detached);
  }
}

int main()
{
  int result = 0;
  asio::io_context io_context;
  asio::signal_set signals(io_context, SIGINT, SIGTERM);
  auto on_done = [&io_context, &result](std::exception_ptr error) {
    if (error) {
      result = 1;
      std::cerr << "fatal error: " << rstream::core::throwable::to_string(error) << std::endl;
    }
    io_context.stop();
  };
  signals.async_wait([&on_done](const boost::system::error_code&, int) { on_done(nullptr); });
  asio::co_spawn(io_context, listener(), on_done);
  io_context.run();
  return result;
}
```

The Asio event loop belongs to the application. The [build guide](docs/000-getting-started.md#link-an-application) provides the CMake file for this program, including runtime plugin selection.

## Documentation

| Task | Guide |
| --- | --- |
| Build and run a first application | [Getting started](docs/000-getting-started.md) |
| Set credentials, contexts or transport options | [Configuration](docs/004-configuration.md) |
| Dial privately or choose another completion style | [Tunnel patterns](docs/005-tunnel-patterns.md) |
| Configure a URI | [URI model](docs/002-uri-model.md) |
| Use the packaged utilities | [Command-line tools](docs/006-tools.md) |
| Understand platform, plugin and packaging constraints | [SDK engineering](docs/001-sdk-engineering.md) |
| Develop the SDK | [Documentation index](docs/README.md) and [Contributing](CONTRIBUTING.md) |

## Security and support

Use [Security](SECURITY.md) for private vulnerability reporting. Usage questions and reproducible bugs can be reported in [GitHub issues](https://github.com/rstreamlabs/rstream-cpp/issues); remove credentials and customer details first. Licensed under [Apache-2.0](LICENSE).
