# rstream-cpp

`rstream-cpp` is the C++ SDK for **rstream**. It targets native environments where performance, control over runtime behavior, and deep integration with existing networking stacks matter, while keeping an API surface that fits naturally into modern asynchronous C++.

The Go SDK is the reference implementation and covers the full rstream API surface. The C++ SDK follows the same platform model and the same agent-to-edge architecture, with a different implementation focus: high-performance native integration and operational bytestream tunnel workflows. If an application already uses Boost.Asio (or Boost.Beast) and needs rstream connectivity without introducing an external process, `rstream-cpp` is designed for that integration style.

## What is rstream?

rstream is a secure connectivity platform built around an edge network and outbound agents. Agents connect outbound from private or local environments, then the edge authenticates traffic, applies access policy, and routes requests to upstream services.

This model avoids inbound exposure on infrastructure while keeping publication, policy, and observability centralized.

## What is a tunnel?

A tunnel is an outbound secure path from an upstream service to the rstream edge. It allows controlled exposure without opening inbound ports on the service side.

Published tunnels provide forwarding addresses for standard clients. Private tunnels are resolved by tunnel id or name and are consumed through rstream-native dial flows.

## How rstream works

The client side establishes and maintains outbound connectivity to the edge. The edge receives downstream traffic, authenticates and evaluates access policies, then forwards traffic over the active tunnel to the upstream workload.

In this model, transport setup remains centralized while application workloads stay in private networks.

## What this SDK is for

`rstream-cpp` is intended for teams that build native services, gateways, agents, or device software in C or C++. The SDK is compatible with **bytestream tunnels** and is optimized for long-lived, operational connectivity inside native processes.

For broader protocol coverage and the most complete tunnel lifecycle features, use the Go SDK: https://github.com/rstreamlabs/rstream-go.

## Tunnel model in rstream-cpp

The SDK supports both published and private tunnel patterns. Published tunnels expose forwarding addresses through the edge. Private tunnels remain non-public and are consumed through rstream endpoint semantics (id or name on an engine).

Tunnel configuration includes labels and policy-related settings. Those properties are carried by the SDK and applied when creating tunnels, while policy enforcement is performed by the rstream edge.

The API surface carries both bytestream and datagram-oriented tunnel properties. Operational compatibility in `rstream-cpp` is focused on bytestream tunnel workflows.

## Boost.Asio integration

`rstream-cpp` follows the Boost.Asio asynchronous model. Core objects use executors, strands, and `async_initiate`, which makes integration straightforward in existing Asio codebases.

Beast-based projects are naturally supported, but the scope is broader: any networking stack organized around Asio abstractions can typically introduce rstream sockets and tunnels without changing architectural patterns.

## URI-centric configuration

URIs are a first-class configuration mechanism in this SDK. Query parameters encode tunnel properties, client options, transport behavior, and TLS settings.

A dedicated reference is available in [docs/URI_MODEL.md](docs/URI_MODEL.md).

## Compatibility

`rstream-cpp` builds on Linux, macOS, and Windows.

## Build prerequisites

A typical source build uses a C++20 compiler, CMake, Conan 2.x, Python 3.x, and usually Ninja as build generator.

Dependencies are resolved through Conan/CMake integration and include Boost, OpenSSL or LibreSSL, yaml-cpp, nlohmann_json, spdlog, plus optional components such as ncurses, maxminddb, and Python bindings.

## Build from source

For a local developer build, configure, build, run checks, and install with standard CMake commands.

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
cmake --build build --target check
cmake --install build --prefix ./build/release
```

For a Conan-managed package build, use the repository Conan configuration.

```bash
conan profile detect --force
conan config install conan/config
conan create . -u --build=missing -pr:b default -pr:h default
```

For cross-platform packaged artifacts, this repository provides `build-conan-cross.sh` and `deploy.py` to produce standalone deliverables under `out/release/...`.

## Using the SDK in a CMake project

After installation (or Conan package consumption), integrate through `find_package` and link `rstream::rstream`. This target pulls the SDK components and their public dependencies.

```cmake
find_package(rstream CONFIG REQUIRED)
add_executable(my_app main.cpp)
target_link_libraries(my_app PRIVATE rstream::rstream)
```

If dynamic plugin mode is used (`ENABLE_STATIC_PLUGINS=OFF`), deploy plugin modules with the runtime layout.

## Environment variables

The SDK and CLI flows use the same configuration concepts, and the variables below participate in explicit resolution paths inside `io-rstrm`.

For consistency with CLI flows, the following variables are the primary explicit overrides:

- `RSTREAM_CONFIG`: Override the config file path. If unset, the SDK falls back to `~/.rstream/config.yaml`.
- `RSTREAM_CONTEXT`: Select the active context during config-based resolution.
- `RSTREAM_API_URL`: Override the control-plane API URL used by context-aware resolution paths.
- `RSTREAM_ENGINE_ADDRESS`: Highest-priority engine address override for runtime connection targets.
- `RSTREAM_ENGINE`: Secondary engine override; when provided as `host:port`, it is expanded to a default `tcp://...` URI with TLS and ALPN defaults.
- `RSTREAM_AUTHENTICATION_TOKEN`: Override token resolution when token behavior is not fixed in SDK options.

### Resolution behavior

In short, engine resolution starts from explicit environment overrides and then falls back to config/context resolution. Token resolution starts from explicit SDK options, then environment override, then config-derived values.

## Code examples

The samples below illustrate the two common patterns: publishing an HTTP server through the edge, and using a private tunnel with a programmatic dial. Both use the same asynchronous style that integrates with existing Asio-based code.

### Published HTTP server over io-rstrm

This sample creates a published HTTP tunnel, prints the forwarding address, then serves HTTP requests using Boost.Beast over an `io_rstrm` socket. The tunnel accept loop keeps accepting edge connections and hands them to per-connection sessions.

```cpp
#include <iostream>
#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <rstream/core/exception.hpp>
#include <rstream/io-rstrm/client.hpp>
#include <rstream/io-rstrm/io-rstrm.hpp>
#include <rstream/io-rstrm/socket.hpp>

namespace asio = boost::asio;
namespace http = boost::beast::http;
namespace rstrm = rstream::io_rstrm;

asio::awaitable<void> session(rstrm::socket socket) {
  try {
    http::request<http::string_body> req;
    boost::beast::flat_buffer buffer;
    co_await http::async_read(socket, buffer, req, asio::use_awaitable);
    http::response<http::string_body> res{http::status::ok, req.version()};
    res.set(http::field::content_type, "text/plain");
    res.keep_alive(req.keep_alive());
    res.body() = "Hello from rstream-cpp";
    res.prepare_payload();
    co_await http::async_write(socket, res, asio::use_awaitable);
    boost::system::error_code ec;
    socket.close(ec);
  } catch (const std::exception& e) {
    std::cerr << "session error: " << e.what() << std::endl;
  }
}

asio::awaitable<void> listener() {
  auto executor = co_await asio::this_coro::executor;
  rstrm::client client(executor);
  co_await client.async_connect(asio::use_awaitable);
  rstrm::tunnel_properties properties = {.m_publish = true, .m_protocol = rstrm::protocol::http};
  auto tunnel = co_await client.async_create_tunnel(properties, asio::use_awaitable);
  auto forwarding = rstrm::format_forwarding_address(tunnel.properties());
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

int main() {
  asio::io_context io_context;
  asio::signal_set signals(io_context, SIGINT, SIGTERM);
  auto on_done = [&io_context](std::exception_ptr error) {
    if (error) {
      std::cerr << "fatal error: " << rstream::core::throwable::to_string(error) << std::endl;
    }
    io_context.stop();
  };
  signals.async_wait([&on_done](const boost::system::error_code&, int) { on_done(nullptr); });
  asio::co_spawn(io_context, listener(), on_done);
  io_context.run();
  return 0;
}
```

### Private tunnel with programmatic dial

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

### Mixed completion styles

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

## CLI tools built from this SDK

This repository also builds native tools that reuse the same transport and tunnel primitives:

`rstream-tunnel` is the C++ alternative to `rstream forward` for tunnel creation and forwarding workflows.

`rstream-ncat` provides netcat-like capabilities with explicit compatibility for rstream tunnel transport paths.

`rstream-nperf-client` and `rstream-nperf-server` provide network performance testing utilities.

`rstream-rtty-client` and `rstream-rtty-server` implement **rstream WebTTY** in C++.

`rstream-runpy` and `rstream-gping` are available when Python bindings are enabled, and `rstream-inspect` is built in Debug configurations.

## rstream-tunnel command examples

The `rstream-tunnel` CLI uses the same tunnel model as the SDK. The examples below are practical starting points for local forwarding, private tunnels, and edge policy options.

To publish a local HTTP service:

```bash
rstream-tunnel 127.0.0.1:8080 --http --publish
```

To expose a private TLS tunnel with a stable tunnel name:

```bash
rstream-tunnel 127.0.0.1:22 --tls --no-publish --name ssh-private
```

To enable edge authentication and basic network policy controls on an HTTP tunnel:

```bash
rstream-tunnel 127.0.0.1:8080 --http --token-auth --rstream-auth --challenge-mode --trusted-ips 203.0.113.0/24,198.51.100.12/32 --geoip FR,US
```

To configure terminated TLS publication with mTLS and explicit cipher policy:

```bash
rstream-tunnel 127.0.0.1:8443 --tls --tls-mode terminated --tls-min-version tls1.2 --tls-ciphers TLS_AES_128_GCM_SHA256,TLS_AES_256_GCM_SHA384 --mtls --mtls-cacert-file ./ca.pem --label env=prod --label app=api
```

## References

- Documentation: https://rstream.io/docs
- Go SDK (reference implementation): https://github.com/rstreamlabs/rstream-go
- C++ SDK: https://github.com/rstreamlabs/rstream-cpp

## Contributing

Pull requests are encouraged and appreciated. Whether you're fixing bugs, adding features, improving documentation, or suggesting enhancements, your contributions help make rstream better for everyone. Build locally, run checks, and submit focused pull requests with clear validation notes.

## Support

**Get help:**  
support@rstream.io

**Report security concerns:**  
reports@rstream.io

## License

See `LICENSE` and `COPYING` in the repository root.
