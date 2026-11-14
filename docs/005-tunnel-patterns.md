# C++ tunnel patterns

The [repository overview](../README.md#serve-http-with-boostbeast) contains a complete HTTP server. These patterns use the same Boost.Asio execution model and [configuration](004-configuration.md).

## Published TCP tunnels

Set `m_protocol` to `rstrm::protocol::tcp` for a raw published TCP bytestream. Leave `m_port` unset for an ephemeral address, or set it to a port already reserved by the project through the Control plane:

```cpp
rstrm::tunnel_properties properties = {
    .m_publish = true,
    .m_protocol = rstrm::protocol::tcp,
    .m_port = 10042,
};
```

The SDK does not reserve the port. Published TCP forwards downstream bytes without adding encryption or authentication; use a secure application protocol such as SSH, or choose a TLS tunnel for TLS traffic.

The `rstream-tunnel` utility exposes the same properties:

```console
rstream-tunnel 127.0.0.1:22 --tcp
rstream-tunnel 127.0.0.1:22 --tcp --tcp-port 10042
```

Add `--allow-cross-region-routing` when the tunnel may route downstream traffic through its owner region.

The C++ SDK does not query the control plane to resolve a region name. A caller that already knows the regional engine address can select it directly:

```console
rstream-tunnel 127.0.0.1:22 --tcp \
  --engine 'tcp://regional-engine.example.com:443?ssl&ssl.tlsv13&ssl.alpn_protos=rstrm%2F1'
```

The same low-level override is available to library users through the engine address passed to `make_endpoint(...)`. The engine remains responsible for validating that the project may use the selected region.

## Private tunnel with programmatic dial

This sample creates a private tunnel named `echo`, accepts a connection, then dials the same tunnel from the same process via an endpoint. It demonstrates the private-tunnel semantics and the dial path that avoids published forwarding addresses.

```cpp
#include <array>
#include <iostream>
#include <string>
#include <boost/asio.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/write.hpp>
#include <rstream/io-rstrm/client.hpp>
#include <rstream/io-rstrm/endpoint.hpp>
#include <rstream/io-rstrm/io-rstrm.hpp>
#include <rstream/io-rstrm/socket.hpp>

namespace asio = boost::asio;
namespace rstrm = rstream::io_rstrm;

asio::awaitable<void> run_private_echo() {
  auto executor = co_await asio::this_coro::executor;
  rstrm::client client(executor);
  co_await client.async_connect(asio::use_awaitable);
  rstrm::tunnel_properties properties = {.m_name = std::string("echo"), .m_publish = false, .m_protocol = rstrm::protocol::tls};
  auto tunnel = co_await client.async_create_tunnel(properties, asio::use_awaitable);
  asio::co_spawn(executor, [&]() -> asio::awaitable<void> {
    rstrm::socket peer(executor);
    rstrm::endpoint remote;
    std::array<char, 5> in{};
    co_await tunnel.async_accept(peer, remote, asio::use_awaitable);
    co_await asio::async_read(peer, asio::buffer(in), asio::use_awaitable);
    co_await asio::async_write(peer, asio::buffer(in), asio::use_awaitable);
  }, asio::detached);
  boost::system::error_code ec;
  auto server = client.address(ec);
  if (ec) {
    throw boost::system::system_error(ec);
  }
  rstrm::endpoint endpoint = {.m_id_name = std::string("echo"), .m_server_address = server};
  rstrm::socket socket(executor);
  std::array<char, 5> out{};
  const std::string msg = "hello";
  co_await socket.async_connect(endpoint, asio::use_awaitable);
  co_await asio::async_write(socket, asio::buffer(msg), asio::use_awaitable);
  co_await asio::async_read(socket, asio::buffer(out), asio::use_awaitable);
  std::cout << "Echo: " << std::string(out.data(), out.size()) << std::endl;
  tunnel.close();
}

int main() {
  asio::io_context io_context;
  asio::co_spawn(io_context, run_private_echo(), [](std::exception_ptr error) {
    if (error) {
      try {
        std::rethrow_exception(error);
      } catch (const std::exception& e) {
        std::cerr << e.what() << std::endl;
      }
    }
  });
  io_context.run();
  return 0;
}
```

## Mixed completion styles

If a codebase mixes completion styles, the same API surface can be used with callbacks, coroutines, or futures.

```cpp
#include <future>
#include <iostream>
#include <boost/asio.hpp>
#include <rstream/io-rstrm/client.hpp>

void demo_tokens(const boost::asio::any_io_executor& executor) {
  rstream::io_rstrm::client client(executor);
  client.async_connect([](const boost::system::error_code& ec) {
    if (ec) {
      std::cerr << ec.message() << std::endl;
    }
  });
  std::future<void> future_connect = client.async_connect(boost::asio::use_future);
  future_connect.get();
}
```

