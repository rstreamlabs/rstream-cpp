# Build and integrate the C++ SDK

The SDK needs a C++20 compiler, CMake and Conan 2. Platform-specific requirements and supported library/plugin combinations are covered by the [engineering guide](001-sdk-engineering.md).

## Build a package

From the repository root, detect a Conan profile if one has not already been configured:

```sh
conan profile detect
```

Install the repository’s Conan configuration, then build the SDK and its package tests:

```sh
conan config install conan/config
conan create . --build=missing -pr:b default -pr:h default
```

The resulting package is in the local Conan cache. Consuming it from another project requires a Conan requirement for the version in `version.txt`, along with CMakeDeps and CMakeToolchain. A configured authenticated remote may also provide the SDK package; its URL and credentials are deployment-specific.

For a source build with dependencies already provisioned, the [Contributing guide](../CONTRIBUTING.md) covers CMake presets and direct builds. It also covers tests; cross-compilation and release packaging are described in the [engineering guide](001-sdk-engineering.md).

## Link an application

Once the SDK and dependencies are available to CMake, save the server from the [README](../README.md#serve-http-with-boostbeast) as `main.cpp` and create:

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

When using Conan, configure with the generated toolchain and matching build type. For an installed SDK, point `CMAKE_PREFIX_PATH` at its install prefix. The `rstream::rstream` target propagates the C++20 requirement and public dependencies. `rstream_enable_runtime_plugins` selects the runtime plugins for either static or dynamic packaging; dynamic builds must retain the installed module layout.

## Configure and run

The SDK reads the rstream CLI’s selected context. Install the [Go CLI](https://github.com/rstreamlabs/rstream-go#start-with-the-cli), then run:

```sh
rstream login
rstream project use my-project-endpoint
```

Replace the endpoint with your project’s endpoint. Build and run `rstream_http_example`; it prints a forwarding address for the in-process HTTP handler. Stop it with an interrupt. For devices without browser login, follow [configuration](004-configuration.md) to supply project-scoped credentials or a certificate identity.

The [tunnel patterns](005-tunnel-patterns.md) guide covers private dialing and additional completion styles. The [URI reference](002-uri-model.md) describes URI-based configuration.
