# Build an application with the C++ SDK

Publish an HTTP/1.1 tunnel and serve a request with Boost.Beast. The program
runs on a Boost.Asio event loop and returns `Hello from rstream`.

## Configure access

With the [rstream CLI](https://rstream.io/docs/getting-started/installation) installed, sign in and approve the request in the browser.

```sh
rstream login
```

List the projects available to this identity.

```sh
rstream project list
```

If the list is empty, open the workspace in the [Dashboard](https://rstream.io/dashboard).
Workspace owners and admins can select **Create new project**. A Basic project
supports this HTTP example. Other members can ask an owner or admin for access
to an existing project. If the CLI login was limited to specific projects, run
`rstream login` again to include the intended project. Run `rstream project list`
again after gaining access.

Choose a project that allows HTTP publication. Replace `my-project-endpoint`
with its `ENDPOINT` value.

```sh
rstream project use my-project-endpoint
```

For a self-hosted Community Edition Engine, select its [direct Engine context](002-configuration.md#connect-to-a-direct-engine).
The SDK reads the same context when it connects.

<a name="get-the-complete-example"></a>

## Create the application

Use a C++20 compiler, Python 3 with Conan 2.34, CMake 3.23 or newer and Ninja.
On Windows, use a Visual Studio Developer PowerShell with the C++ tools installed.
Create an empty directory and save the following three files in it.

**`main.cpp`**

```cpp
#include <csignal>
#include <exception>
#include <iostream>

#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <rstream/core/exception.hpp>
#include <rstream/io-rstrm/client.hpp>
#include <rstream/io-rstrm/socket.hpp>

namespace asio = boost::asio;
namespace http = boost::beast::http;
namespace rstrm = rstream::io_rstrm;

asio::awaitable<void> respond(rstrm::socket& socket)
{
  boost::beast::flat_buffer buffer;
  http::request<http::string_body> request;
  co_await http::async_read(socket, buffer, request, asio::use_awaitable);

  http::response<http::string_body> response{http::status::ok, request.version()};
  response.set(http::field::content_type, "text/plain");
  response.keep_alive(false);
  response.body() = "Hello from rstream";
  response.prepare_payload();
  co_await http::async_write(socket, response, asio::use_awaitable);
}

asio::awaitable<void> serve(rstrm::client& client, rstrm::socket& socket, const bool& stopping)
{
  co_await client.async_connect(asio::use_awaitable);
  rstrm::tunnel_properties properties{};
  properties.m_protocol = rstrm::protocol::http;
  properties.m_http_version = "http/1.1";
  properties.m_publish = true;
  auto tunnel = co_await client.async_create_tunnel(properties, asio::use_awaitable);
  std::cout << rstrm::format_forwarding_address(tunnel.properties()).value() << std::endl;

  while (!stopping) {
    rstrm::endpoint peer;
    co_await tunnel.async_accept(socket, peer, asio::use_awaitable);
    try {
      co_await respond(socket);
    }
    catch (const std::exception& error) {
      if (!stopping) {
        std::cerr << error.what() << '\n';
      }
    }
    boost::system::error_code ignored;
    socket.close(ignored);
  }
}

int main()
{
  asio::io_context io;
  rstrm::client client(io.get_executor());
  rstrm::socket socket(io.get_executor());
  asio::signal_set signals(io, SIGINT, SIGTERM);
  bool stopping = false;
  signals.async_wait([&](auto error, int) {
    if (!error) {
      stopping = true;
      client.close();
      boost::system::error_code ignored;
      socket.close(ignored);
    }
  });
  int status = 0;
  asio::co_spawn(io, serve(client, socket, stopping), [&](std::exception_ptr error) {
    if (error && !stopping) {
      std::cerr << rstream::core::throwable::to_string(error) << '\n';
      status = 1;
    }
    signals.cancel();
    client.close();
  });
  io.run();
  return status;
}
```

**`conanfile.py`**

```python
from conan import ConanFile
from conan.tools.cmake import CMakeDeps, CMakeToolchain, cmake_layout


class HttpServer(ConanFile):
    settings = "os", "compiler", "build_type", "arch"
    requires = "rstream/1.15.3", "boost/[>=1.81.0 <1.90.0]"
    default_options = {
        "boost/*:without_cobalt": True,
        "rstream/*:build_bins": False,
        "rstream/*:enable_testing": False,
        "rstream/*:with_maxminddb": False,
        "rstream/*:with_ncurses": False,
    }

    def layout(self):
        cmake_layout(self, generator="Ninja")

    def generate(self):
        CMakeToolchain(self, generator="Ninja").generate()
        CMakeDeps(self).generate()
```

**`CMakeLists.txt`**

```cmake
cmake_minimum_required(VERSION 3.23)
if(POLICY CMP0167)
  cmake_policy(SET CMP0167 NEW)
endif()
project(rstream-hello LANGUAGES CXX)

find_package(Boost REQUIRED)
find_package(rstream CONFIG REQUIRED)
add_executable(rstream-hello main.cpp)
target_compile_features(rstream-hello PRIVATE cxx_std_20)
target_link_libraries(rstream-hello PRIVATE Boost::boost rstream::rstream)
rstream_enable_runtime_plugins(
  rstream-hello
  rstream::plugin::io-generic
  rstream::plugin::io-rstrm)
install(TARGETS rstream-hello RUNTIME DESTINATION bin)
```

## Build and run

Detect a default Conan profile or keep the existing one.

```sh
conan profile detect --exist-ok
```

If the public rstream remote is missing from `conan remote list`, register it.

```sh
conan remote add rstream https://nexus.rstream.io/repository/conan
```

From the directory containing the three files, resolve dependencies, build
the Release target and install it under `out/`.

```sh
conan install . --build=missing -s:h build_type=Release -s:h compiler.cppstd=20 -s:b compiler.cppstd=20
cmake --preset conan-release
cmake --build --preset conan-release
cmake --install build/Release --config Release --prefix out
```

On macOS or Linux, start the server.

```sh
./out/bin/rstream-hello
```

On Windows, run `.\out\bin\rstream-hello.exe` in PowerShell.

## Verify the application

Open the printed HTTPS address while the server is running. An HTTP GET
returns HTTP 200 and `Hello from rstream`. For a protected endpoint, use
the [configured caller credential](https://rstream.io/docs/tunnels/http-tunnels/authentication#token-authentication).
With a private CA, configure the HTTP client to trust that CA and keep the
printed hostname for certificate verification. Stop the server with Ctrl+C.

## Link an existing CMake target

Use the same Conan SDK requirement as the example, or provide an installed
SDK and its public dependencies through the application's CMake prefix.

```cmake
find_package(rstream CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE rstream::rstream)
rstream_enable_runtime_plugins(
  my_app
  rstream::plugin::io-generic
  rstream::plugin::io-rstrm)
```

The target propagates C++20 and the SDK's public dependencies. The helper
selects the transport plugins used by the application. See [plugins and
packaging](009-plugins-and-packaging.md) when distributing the binary, and
[the Beast migration](004-http-tls-websocket.md#move-a-beast-service-from-local-tcp-to-a-tunnel)
to adapt an existing listener.
