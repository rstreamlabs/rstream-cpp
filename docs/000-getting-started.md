# Build and integrate the C++ SDK

This guide builds a small application against the published SDK. You need a
C++20 compiler, CMake, Ninja and Conan 2. Ninja is required when Conan builds
the SDK from source. Platform requirements and library/plugin
combinations are covered by the [engineering guide](001-sdk-engineering.md).

## Install the SDK

Create an application directory. Detect a Conan profile if you do not already
have one, then add the rstream remote:

```sh
mkdir rstream-http
cd rstream-http
conan profile detect
conan remote add rstream https://nexus.rstream.io/repository/conan -f
```

Save this application recipe as `conanfile.py`. It selects SDK `1.15.1` and
omits the SDK's utility binaries and optional GeoIP and terminal dependencies:

```python
from conan import ConanFile


class Application(ConanFile):
    settings = "os", "compiler", "build_type", "arch"
    generators = "CMakeDeps", "CMakeToolchain"
    requires = "boost/[>=1.81.0 <1.90.0]", "rstream/1.15.1"
    default_options = {
        "boost/*:without_cobalt": True,
        "rstream/*:build_bins": False,
        "rstream/*:enable_testing": False,
        "rstream/*:with_maxminddb": False,
        "rstream/*:with_ncurses": False,
    }
```

Conan downloads a compatible binary when available and builds missing packages
from source. The Boost range matches the maintained sample's supported range.

## Link an application

Save the complete [README server](../README.md#serve-http-with-boostbeast) as
`main.cpp` and create `CMakeLists.txt` beside it:

```cmake
cmake_minimum_required(VERSION 3.16)
project(rstream_http_example LANGUAGES CXX)
find_package(rstream CONFIG REQUIRED)
add_executable(rstream_http_example main.cpp)
target_link_libraries(rstream_http_example PRIVATE rstream::rstream)
rstream_enable_runtime_plugins(
  rstream_http_example
  rstream::plugin::io-generic
  rstream::plugin::io-rstrm)
```

Install dependencies and build with matching Conan and CMake settings:

```sh
conan install . --output-folder=build --build=missing -s build_type=Release -s compiler.cppstd=20
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=conan_toolchain.cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

The `rstream::rstream` target propagates C++20 and its public dependencies.
`rstream_enable_runtime_plugins` selects runtime plugins for static or dynamic
packaging; dynamic builds must retain the installed module layout.

## Configure and run

Install the [rstream CLI](https://rstream.io/docs/getting-started/installation),
then select a project using its endpoint from the dashboard:

```sh
rstream login
rstream project use my-project-endpoint
./build/rstream_http_example
```

Replace `my-project-endpoint` with your own endpoint. With a multi-configuration
generator such as Visual Studio, run `build/Release/rstream_http_example.exe`.
The program prints a forwarding address for its in-process HTTP handler; open
that address and stop the program with an interrupt. For devices without
browser login, see [configuration](004-configuration.md) for project-scoped
credentials or a certificate identity.

The [tunnel patterns](005-tunnel-patterns.md) guide covers private dialing and
additional completion styles. The [URI reference](002-uri-model.md) describes
URI-based configuration.

## Build or install from source

To modify the SDK, follow [Contributing](../CONTRIBUTING.md) for source builds
and tests. From the SDK repository, `conan config install conan/config` installs
its build profiles, and `conan create . --build=missing -pr:b default -pr:h default`
builds the package in the local cache. Cross-compilation and release packaging
are covered by the [engineering guide](001-sdk-engineering.md).

If your organization provisions an installed SDK, configure the application
with `CMAKE_PREFIX_PATH` pointing to that installation and provide its public
dependencies through your build system. The same CMake targets and server
source apply.
