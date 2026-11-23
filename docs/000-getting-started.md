# Build an application with the C++ SDK

This Boost.Beast server opens an HTTP tunnel, prints its forwarding address
and responds with the machine's hostname. Build it in an empty directory
with a C++20 compiler, Python 3, Conan 2.34, CMake 3.23 or newer, and
Ninja. On Windows, use a Developer PowerShell for Visual Studio.

## Configure the connection

For hosted rstream, use an [installed CLI](https://rstream.io/docs/getting-started/installation)
and a project that permits published HTTP. Sign in with the CLI.

```sh
rstream login
```

List your projects.

```sh
rstream project list
```

Replace `my-project-endpoint` with the value from the `ENDPOINT` column.

```sh
rstream project use my-project-endpoint
```

For a self-hosted deployment, select a [direct Engine context](002-configuration.md#connect-to-a-direct-engine)
with its address, credential and private CA if required. The application reads
the selected context on startup.

<a id="get-the-complete-example"></a>

## Write the HTTP server

Save the following as `main.cpp`. Each accepted socket runs a Beast session
on the application's Asio event loop.

```cpp
#include <csignal>
#include <iostream>
#include <string>

#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>

#include <rstream/core/exception.hpp>
#include <rstream/io-rstrm/client.hpp>
#include <rstream/io-rstrm/io-rstrm.hpp>
#include <rstream/io-rstrm/socket.hpp>

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
namespace rstrm = rstream::io_rstrm;

asio::awaitable<void> serve_session(rstrm::socket socket)
{
  try
  {
    beast::flat_buffer buffer;
    http::request<http::string_body> request;
    co_await http::async_read(socket, buffer, request, asio::use_awaitable);
    http::response<http::string_body> response{http::status::ok, request.version()};
    response.set(http::field::content_type, "text/plain; charset=utf-8");
    response.keep_alive(false);
    response.body() = asio::ip::host_name();
    response.prepare_payload();
    co_await http::async_write(socket, response, asio::use_awaitable);
    boost::system::error_code ignored;
    socket.close(ignored);
  }
  catch (const std::exception& error)
  {
    std::cerr << "session error: " << error.what() << std::endl;
  }
}

asio::awaitable<void> run_server(rstrm::client& client)
{
  auto executor = co_await asio::this_coro::executor;
  co_await client.async_connect(asio::use_awaitable);
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
}

int main()
{
  int exit_code = 0;
  bool stopping = false;
  asio::io_context io_context;
  rstrm::client client(io_context.get_executor());
  asio::signal_set signals(io_context, SIGINT, SIGTERM);
  signals.async_wait([&](const boost::system::error_code& error, int) {
    if (!error) {
      stopping = true;
      client.close();
    }
  });
  asio::co_spawn(io_context, run_server(client), [&](std::exception_ptr error) {
    if (error && !stopping) {
      std::cerr << "fatal error: " << rstream::core::throwable::to_string(error) << std::endl;
      exit_code = 1;
    }
    signals.cancel();
    client.close();
  });
  io_context.run();
  return exit_code;
}
```

## Configure the build

Place `conanfile.py` next to `main.cpp`. Conan installs the published SDK
and Boost and generates the CMake presets.

```python
from conan import ConanFile
from conan.tools.cmake import CMakeDeps, CMakeToolchain, cmake_layout


class ConanPackage(ConanFile):
    name = "rstream-tunnels-basic-http-server"
    version = "1.0.0"
    settings = "os", "compiler", "build_type", "arch"
    requires = (
        "rstream/1.15.3",
        "boost/[>=1.81.0 <1.90.0]",
    )

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

Save the following as `CMakeLists.txt`. The two plugins provide the
transports the application needs at runtime.

```cmake
cmake_minimum_required(VERSION 3.22)
if(POLICY CMP0167)
  cmake_policy(SET CMP0167 NEW)
endif()

project(rstream-tunnels-basic-http-server LANGUAGES CXX)

include(GNUInstallDirs)
find_package(Boost REQUIRED)
find_package(rstream CONFIG REQUIRED)

add_executable(${PROJECT_NAME} main.cpp)
target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_20)
target_link_libraries(${PROJECT_NAME} PRIVATE Boost::boost rstream::rstream)
rstream_enable_runtime_plugins(
  ${PROJECT_NAME}
  rstream::plugin::io-generic
  rstream::plugin::io-rstrm)
install(TARGETS ${PROJECT_NAME} RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
```

## Build the application

Prepare a Conan profile and register the rstream package remote.

```sh
conan profile detect --exist-ok
conan remote add rstream https://nexus.rstream.io/repository/conan -f
```

From the project directory, resolve the dependencies with C++20 enabled
for the host and build profiles.

```sh
conan install . --build=missing -s:h build_type=Release -s:h compiler.cppstd=20 -s:b compiler.cppstd=20
```

Configure, build and install the executable and its runtime files.

```sh
cmake --preset conan-release
cmake --build --preset conan-release
cmake --install build/Release --config Release --prefix out
```

## Verify the application

Start the installed binary on macOS or Linux.

```sh
./out/bin/rstream-tunnels-basic-http-server
```

On Windows, start the executable in PowerShell.

```powershell
.\out\bin\rstream-tunnels-basic-http-server.exe
```

Open the HTTPS URL printed after `Forwarding address:`. Expect HTTP 200
and the hostname of the machine running the program. With a private CA,
configure the HTTP client to trust it. If the project restricts access,
use the [required caller credential](https://rstream.io/docs/tunnels/http-tunnels/authentication#token-authentication).
Press Ctrl+C in the server terminal to close the tunnel.

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
