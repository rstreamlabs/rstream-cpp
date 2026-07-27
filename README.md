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

## Installation (rstream binaries)

The manual installer below installs packaged binaries produced from this SDK (the `rstream-utils` package). It installs operational tools such as `rstream-tunnel`, not the C++ SDK headers/libraries used for application development.

```bash
PACKAGE_NAME="rstream-utils" /bin/bash -i -c "$(curl -fsSL https://rstream.io/scripts/install.sh)"
```

If you want to integrate the SDK itself in a C++ codebase, use the build and package paths described in the next sections.

## Build prerequisites

A typical source build uses a C++20 compiler, CMake, Conan 2.x, Python 3.x, and usually Ninja as build generator.

Dependencies are resolved through Conan/CMake integration and include Boost, OpenSSL or LibreSSL, yaml-cpp, nlohmann_json, spdlog, plus optional components such as ncurses, maxminddb, and Python bindings.

The supported platform matrix, Boost.Asio and rstream runtime contracts,
Conan Center dependency policy, constrained-system requirements, and complete
validation procedure are defined in
[docs/SDK_ENGINEERING.md](docs/SDK_ENGINEERING.md).

## Build from source

The recommended source build uses Conan to provision third-party dependencies and then builds the package:

```bash
conan profile detect --force
conan config install conan/config
conan create . -u --build=missing -pr:b default -pr:h default
```

If Boost, protobuf, OpenSSL, yaml-cpp, nlohmann_json, spdlog, and test dependencies are already available through your system package manager or local CMake package registry, a direct CMake build also works:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
cmake --install build --prefix ./build/release
```

Native development presets provide reproducible development, quality,
sanitizer, coverage, and Release configurations:

```bash
cmake --preset quality
cmake --build --preset quality
ctest --preset quality
```

The `release`, `release-shared-static`, `release-static-static`, and
`release-static-dynamic` presets cover the four library/plugin topologies.
`build.sh` exposes the same choices through `build_shared=on|off` and
`plugin_mode=auto|static|dynamic`.

Runtime smoke tests that need a reachable rstream engine live under `test/e2e`. After building `rstream-tunnel`, the engine-only suites can run against any configured context:

```bash
export RSTREAM_CONTEXT=<context>
test/e2e/rstream-tunnel-passthrough.sh
test/e2e/rstream-tunnel-runtime.sh
```

Set `RSTREAM_TUNNEL_BIN` when the binary is not under a standard build directory.

The mTLS and PKCS#11 runtime suites also create temporary Control plane credential resources before opening tunnels. They require an explicit Control plane API URL and PAT:

```bash
export RSTREAM_RUNTIME_API_URL=<control-plane-api-url>
export RSTREAM_RUNTIME_CONTROL_TOKEN='<pat with credential and project read permissions>'
export RSTREAM_RUNTIME_PRO_PROJECT_ENDPOINT=<pro-project-endpoint>
test/e2e/rstream-tunnel-mtls-runtime.sh
test/e2e/rstream-tunnel-pkcs11-runtime.sh
```

Those scripts intentionally do not fall back to a local Control plane URL or to the engine context token; use the engine-only suites when validating remote devices that cannot reach the Control plane.

For cross-platform packaged artifacts, this repository provides `build-conan-cross.sh`, `build-docker-conan.sh`, and `deploy.py` to produce standalone deliverables under `out/release/...`.

Library linkage and plugin loading are independent build choices. CMake uses
`BUILD_SHARED_LIBS` for SDK libraries and `ENABLE_STATIC_PLUGINS` for plugin
registration. The cross-build script exposes the same contract through
`*_BUILD_SHARED` and `*_PLUGIN_MODES` for Linux, macOS, and Windows.

Plugin mode defaults to `auto`, which preserves the historical packaging:
static libraries use static plugins and shared libraries use dynamic plugins.
Use `static` or `dynamic` explicitly to build another supported combination:

```bash
OSS="linux windows" \
LINUX_ARCHS="x86_64" \
LINUX_BUILD_SHARED="on off" \
LINUX_PLUGIN_MODES="static dynamic" \
WINDOWS_ARCHS="x86_64" \
WINDOWS_BUILD_SHARED="on off" \
WINDOWS_PLUGIN_MODES="static dynamic" \
./build-conan-cross.sh
```

The cross-build script enables strict project warnings and treats them as
errors by default. Set `ENABLE_STRICT_WARNINGS=off WARNINGS_AS_ERRORS=off` only
for diagnosis against a compiler that is not yet part of the supported
matrix.

Regular CI is handled by the `Build` GitHub Actions workflow. Pushes to `main` build the `stable` Conan channel, `feature-*` and `fix-*` branches build the `dev` channel, and manual runs can select `dev`, `stable`, or `testing`. This workflow validates all four library/plugin topologies on Linux, macOS, and Windows with `conan create`.

The scheduled `Reliability` workflow reruns that complete package matrix and adds AddressSanitizer, UndefinedBehaviorSanitizer, ThreadSanitizer, static analysis, and repeated concurrency/lifecycle tests. Its CMake presets and test selection are available locally:

```bash
cmake --preset asan
cmake --build --preset asan
ctest --preset asan

cmake --preset tsan
cmake --build --preset tsan
ctest --preset tsan

ctest --test-dir out/build/quality --repeat until-fail:20 --output-on-failure \
  -R 'core-(executor-binder|plugin-version)|io-common-(payloader-limits|queue|stream-tcp)|io-rstrm-(control-channel|handshake)|nperf-runtime|tunnel-proxy|webtty-.*runtime'
```

Release packaging is handled separately by the `Release Packages` workflow. Manual dispatch builds the same `rstream-utils` artifacts as:

```bash
EXPORT_PACKAGE_NAME="rstream-utils" LINUX_TCLIBCS="musl" LINUX_BUILD_SHARED="off" MACOS_BUILD_SHARED="on" WINDOWS_BUILD_SHARED="off" ./build-conan-cross.sh
```

Linux release binaries use the musl static package path for broad distribution compatibility. glibc exports are tied to the configured Yocto toolchain baseline and carry that libc version in package metadata; only publish them when that baseline is intentionally part of the target environment.

The workflow runs macOS packaging on a macOS runner and Linux/Windows packaging on Ubuntu runners. Manual runs upload only when the `upload` input is enabled; release tags upload automatically to the stable channel after the tag and Conan package version have been checked. Package upload requires the `RSTREAM_TOKEN` repository secret.

macOS binaries are signed inside `deploy.py` before the release archive is compressed. Signing is enabled for release tags and manual upload runs through `MACOS_CODESIGN_MODE=certificate`, `MACOS_CODESIGN_TOOL=rcodesign`, `MACOS_CERTIFICATE`, `MACOS_CERTIFICATE_PWD`, and `MACOS_APP_STORE_API_KEY`. Leave `MACOS_CODESIGN_MODE` unset for local, unsigned packaging.

On release tags, the same workflow also validates the Conan recipe with `conan create` and publishes only the Conan source package to the configured Conan remote with `conan upload --only-recipe`. This requires `CONAN_REMOTE_URL`, `CONAN_REMOTE_USERNAME`, and optionally `CONAN_REMOTE_NAME` repository variables, plus the `CONAN_PASSWORD` repository secret. Missing Conan publication credentials fail the release job.

If your packaging flow needs an authenticated Conan remote, configure it through environment variables instead of editing repository files:

- `CONAN_REMOTE_URL`
- `CONAN_REMOTE_USERNAME`
- `CONAN_REMOTE_NAME` (optional, defaults to `rstream`)
- `CONAN_PASSWORD_FILE` (used by `build-docker-conan.sh` and `conan/compose.yaml`, defaults to `~/.credentials/conan`)

When those variables are unset, local Docker packaging and non-upload CI runs use the existing Conan setup.

## Using the SDK in a CMake project

After installation (or Conan package consumption), integrate through `find_package` and link `rstream::rstream`. This target pulls the SDK components and their public dependencies.

```cmake
find_package(rstream CONFIG REQUIRED)
add_executable(my_app main.cpp)
target_link_libraries(my_app PRIVATE rstream::rstream)
```

If dynamic plugin mode is used (`ENABLE_STATIC_PLUGINS=OFF`), deploy plugin modules with the runtime layout.

## Authentication and project setup

Before using SDK or CLI flows, login and project/context setup are required through the `rstream` CLI. The standard developer-machine path is browser-based login:

```bash
rstream login
```

Then select the target project endpoint and set the default context:

```bash
rstream project use <project-endpoint> --default
```

`rstream-cpp` consumes the same configuration file and context model as the `rstream` CLI (default path: `~/.rstream/config.yaml`).

For advanced authentication modes and context workflows, see the rstream CLI workflow documentation:

- https://github.com/rstreamlabs/rstream-go/blob/main/docs/CLI_WORKFLOW.md

## Environment variables

The SDK and CLI flows use the same configuration concepts, and the variables below participate in explicit resolution paths inside `io-rstrm`.

For consistency with CLI flows, the following variables are the primary explicit overrides:

- `RSTREAM_CONFIG`: Override the config file path. If unset, the SDK falls back to `~/.rstream/config.yaml`.
- `RSTREAM_CONTEXT`: Select the active context during config-based resolution.
- `RSTREAM_API_URL`: Override the Control plane API URL used by context-aware resolution paths.
- `RSTREAM_ENGINE_ADDRESS`: Highest-priority engine address override for runtime connection targets.
- `RSTREAM_ENGINE`: Secondary engine override; when provided as `host:port`, it is expanded to a default `tcp://...` URI with TLS and ALPN defaults.
- `RSTREAM_AUTHENTICATION_TOKEN`: Override token resolution when token behavior is not fixed in SDK options.
- `RSTREAM_MTLS_CERT_FILE`: Client certificate file for mTLS agent authentication.
- `RSTREAM_MTLS_KEY_FILE`: Client private key file for mTLS agent authentication.
- `RSTREAM_TUNNEL_TRANSPORT`: Select `auto`, `tls`, or `quic` for the engine connection.
- `RSTREAM_QUIC_TRANSPORT`: Legacy transport selector. Prefer `RSTREAM_TUNNEL_TRANSPORT`.

### Resolution behavior

In short, engine resolution starts from explicit environment overrides and then falls back to config/context resolution. Token resolution starts from explicit SDK options, then environment override, then config-derived values. Token authentication and mTLS agent authentication are mutually exclusive for the control-channel connection. When the mTLS certificate and key variables are set, config-derived tokens are not used for that connection; setting mTLS variables together with `RSTREAM_AUTHENTICATION_TOKEN` is an error. Engine HTTP API requests use token authentication.

The shared YAML schema includes `transport.proxy` for SDKs that can proxy their engine connection. `rstream-cpp` parses that block, including the proxy TLS fields `caFile`, `serverName`, and `insecureSkipVerify`, so configuration files keep the same shape across SDKs. It does not currently implement HTTP CONNECT, SOCKS5, or environment-derived proxying for the `io-rstrm` engine connection. A selected context or environment that requests a proxy fails during configuration resolution instead of being ignored.

The same schema accepts `transport.mode: auto|tls|quic`. The C++ runtime
currently implements TLS only, so the default `auto` mode resolves to TLS.
Explicit `quic`, invalid values, and legacy `transport.useQuic: true` fail
during resolution instead of being silently ignored.

Contexts can authenticate an agent control-channel connection with mTLS instead of a token:

```yaml
contexts:
  - name: prod-mtls
    engine: project.cluster.example:443
    auth:
      mtls:
        certificateFile: /etc/rstream/client.pem
        keyFile: /etc/rstream/client-key.pem
```

Inline `certificate` and `key` values are also supported for generated or ephemeral configs.

## Code examples

The samples below illustrate the two common patterns: publishing an HTTP server through the edge, and using a private tunnel with a programmatic dial. Both use the same asynchronous style that integrates with existing Asio-based code.

### Published HTTP server over io-rstrm

This sample creates a published HTTP tunnel, prints the forwarding address, then serves HTTP requests using Boost.Beast over an `io_rstrm` socket. The tunnel accept loop keeps accepting edge connections and hands them to per-connection sessions.

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

### Published TCP tunnels

Set `m_protocol` to `rstrm::protocol::tcp` for a raw published TCP bytestream. Leave `m_port` unset for an ephemeral address, or set it to a port already reserved by the project through the Control plane:

```cpp
rstrm::tunnel_properties properties = {
    .m_publish = true,
    .m_protocol = rstrm::protocol::tcp,
    .m_port = 10042,
};
```

The SDK does not reserve the port. Published TCP forwards downstream bytes without adding encryption or authentication; use a secure application protocol such as SSH, or choose a TLS tunnel for TLS traffic.

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

`rstream-webtty-client` and `rstream-webtty-server` implement **rstream WebTTY** in C++.

`rstream-webtty-server -v --rstream` serves WebTTY through an rstream tunnel URI with the standard discovery labels, including `application-protocol=rstream.webtty`, `rstream.webtty.capabilities=exec`, and `rstream.webtty.exec.path=/`. Use `--uri` for a direct local listener. The C++ tools expose the SDK URI model in their command line, so the same argument can describe a TCP listener or an `rstrm://` endpoint; the higher-level `rstream webtty` workflow separates listener selection and registered-server resolution.

The WebTTY protobuf contract includes endpoint identities and payload crypto metadata for encrypted sessions. The C++ SDK exposes E2E payload crypto helpers for the nominal suite: AES-256-GCM for stdin/stdout/stderr payloads with a fresh 96-bit nonce per WebTTY data message, and HPKE Base with DHKEM(X25519, HKDF-SHA256), HKDF-SHA256, and AES-256-GCM for wrapping per-session payload keys. Endpoint-authenticated sessions also use ECDSA P-256 with SHA-256 for `ServerHello` and `ClientProof` transcripts. The protobuf enum reserves ChaCha20-Poly1305 identifiers for future negotiation. Current C++ helpers do not accept those suites and fail closed if they are requested.

Managed attach and control transfer are engine-coordinated capabilities. The
C++ WebTTY server accepts direct `Open` sessions and rejects `Attach` handshakes
with a clear protocol error instead of attempting local multi-user routing.

`rstream-runpy` and `rstream-gping` are available when Python bindings are enabled, and `rstream-inspect` is built in Debug configurations.

## rstream-tunnel command examples

The `rstream-tunnel` CLI uses the same tunnel model as the SDK. The examples below are practical starting points for local forwarding, private tunnels, and edge policy options.
When neither `--publish` nor `--no-publish` is provided, the project policy decides the final exposure mode. On projects where public access is forbidden, the tunnel falls back to a private tunnel instead of sending public-only defaults.

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
rstream-tunnel 127.0.0.1:8080 --http --token-auth --challenge-mode --trusted-ips 203.0.113.0/24,198.51.100.12/32 --geoip FR,US
```

To configure terminated TLS publication with mTLS and explicit cipher policy:

```bash
rstream-tunnel 127.0.0.1:8443 --tls --tls-mode terminated --tls-min-version tls1.2 --tls-ciphers TLS_AES_128_GCM_SHA256,TLS_AES_256_GCM_SHA384 --mtls --label env=prod --label app=api
```

mTLS Tunnel access uses mTLS credentials to decide which client certificates are allowed.

To publish a passthrough TLS tunnel, keep edge TLS policy options off so the upstream service owns the certificate and handshake:

```bash
rstream-tunnel 127.0.0.1:8443 --tls --tls-mode passthrough
```

## References

- Documentation: https://rstream.io/docs
- Go SDK (reference implementation): https://github.com/rstreamlabs/rstream-go
- C++ SDK: https://github.com/rstreamlabs/rstream-cpp
- CLI workflow and authentication: https://github.com/rstreamlabs/rstream-go/blob/main/docs/CLI_WORKFLOW.md
- Declarative run workflows: https://github.com/rstreamlabs/rstream-go/blob/main/docs/CMD_RUN.md
- Transport configuration: https://github.com/rstreamlabs/rstream-go/blob/main/docs/TRANSPORT.md
- Tunnel property reference: https://github.com/rstreamlabs/rstream-go/blob/main/docs/TUNNEL_PROPERTIES.md

## Contributing

See [CONTRIBUTING.md](./CONTRIBUTING.md) for the expected local checks, scope guidance, and pull request conventions.

## Support

For support requests, contact `support@rstream.io`.

For security reporting guidance, see [SECURITY.md](./SECURITY.md).

## License

This repository is licensed under the Apache License 2.0. See `LICENSE`.
