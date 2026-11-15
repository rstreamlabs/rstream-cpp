# Develop an existing native service

After the [shared Beast handler migration](004-http-tls-websocket.md#move-a-beast-service-from-local-tcp-to-a-tunnel),
choose the [direct API, acceptor or URI model](003-tunnel-patterns.md) that fits
the service. This guide connects the application decisions to the SDK's deeper
contracts. The [engineering guide](008-sdk-engineering.md) remains the acceptance
baseline for changes to the runtime or packaging.

## Keep execution with the application

Pass the existing executor when constructing a client or socket. A coroutine
can obtain its executor through `co_await asio::this_coro::executor`; callback
code can pass an `io_context` or application strand executor. An operation's
completion handler may select a different associated executor. This does not
move ownership of its buffers or make shared application state thread-safe.

For an application using several event-loop threads, make the session's
serialization explicit. This illustrative callback starts an operation on a
client that remains alive until completion:

```cpp
client.async_connect(asio::bind_executor(session_strand,
    [owner](const boost::system::error_code& error) {
      owner->on_connected(error);
    }));
```

The shared `owner` must represent real session state, not a reference to a
stack frame that is about to return. The [callback program](../example/io-rstrm/callback.cpp)
shows the complete ownership pattern. Keep blocking filesystem/device work and
future waits off loop threads. The [future program](../example/io-rstrm/future.cpp)
creates its own worker and joins it; the SDK does not run that loop for it.

## Choose allocation and lifetime independently

There are two allocation decisions. SDK objects can receive a shared
`core::allocator::ptr` in their constructors for owned internal state. Completion
handlers also have an Asio associated allocator; use `asio::bind_allocator`
when the surrounding application supplies one. The associated allocator does
not by itself own every SDK object or the payload buffer.

A custom object allocator implements `allocate(size_t)` and `deallocate(void*)`.
It must remain valid for outstanding ownership and have behavior appropriate
to the executors accessing it. Reject exhaustion correctly; a small pool is not
a license to return invalid storage. See the [allocator interface](../lib/rstream/core/allocator.hpp)
and [small allocation example](../example/core/common/example_core_allocator.cpp).
That example demonstrates the interface, not a production pool or benchmark.

Move-only handlers and their executor/allocator/cancellation associations are
preserved. Erased completion handlers need special care: their associated
allocator may borrow handler-owned storage. Do not use it to allocate a control
block that can outlive the handler, such as one retained by cancellation state.
The [Asio engineering contract](008-sdk-engineering.md#boostasio-contract) explains
the owning fallback and its required late-cancellation/concurrency tests.

## Make cancellation and shutdown observable

Associate an Asio cancellation slot with an operation when it should be
cancelled independently. Keep the signal and the operation's referenced state
alive for their intended lifetimes. Cancellation is an asynchronous request;
the completion is the point at which the application may release a referenced
buffer. Closing a copied direct socket closes the same shared connection.

A bounded exchange needs both a timeout decision and cleanup. The
[private echo](../example/io-rstrm/private-echo.cpp) races its exchange against a
timer with `experimental::parallel_group`, waits for both completions, propagates
the exchange error, and closes its control client on success and failure. Its
accept and dial halves cancel one another when either fails. They are not
unobserved detached coroutines.

The low-level `client` exposes disconnect/status callbacks; the caller decides
whether to reconnect and create new tunnels. The acceptor can do that itself
using `settings_acceptor::m_auto_reconnect` and `m_auto_recreate_tunnel`, with
bounded retry intervals. Closing a tunnel stops its publication/accept path;
close the control client when it is no longer needed. Do not stop the entire
application loop merely to end one tunnel. A process-level server interrupt in
the examples is deliberately a process shutdown, not a reusable graceful-drain
API.

## Package the selected plugins

Libraries and plugins have independent linkage. `BUILD_SHARED_LIBS` controls
SDK libraries; `ENABLE_STATIC_PLUGINS` controls static registration versus
runtime module loading. All four combinations are supported contracts, not
interchangeable fallbacks.

Use `rstream::rstream` and `rstream_enable_runtime_plugins` from the exported
CMake package. The helper retains the required static registration or stages
dynamic modules. For dynamic deployment preserve the installation layout:
executables and shared libraries must still be able to find their modules under
`lib/rstream`. Do not copy only the executable to a device and assume the
modules or transitive shared libraries are embedded. The [package matrix](008-sdk-engineering.md#conan-package-matrix)
requires external consumption, not compilation alone. `rstream-inspect plugins`
in a Debug tools build can help inspect registered/discovered modules; use
`--help` for its supported search-path options.

The [URI reference](006-uri-model.md) describes which namespace each plugin
owns. A TCP listener does not become an rstream listener because its query
contains `rstream.retry`. Use the rstrm plugin/scheme for the tunnel path and
retain the project/credential configuration separately from untrusted URIs.

## Qualify the target rather than assuming native parity

A successful host build establishes a local development path. A constrained
Linux, musl, Yocto or Windows distribution requires the target's toolchain,
Conan graph, library/plugin topology and runtime execution. The
[engineering platform contract](008-sdk-engineering.md#supported-environments)
and [distribution command](008-sdk-engineering.md#distribution-packaging) define
that matrix. Use the public dependency graph for ordinary SDK consumers;
repository-local recipes are restricted packaging overrides, described by the
[inventory](../conan/recipes/README.md).

Android, Emscripten and MinGW profile refreshes have their own
[roadmap](../roadmap/001-cross-compilation-toolchains.md); their existence is not
runtime qualification for an arbitrary target. Provider/header/runtime versions
must agree. Preserve [critical dependency checks](009-critical-dependencies.md),
ASan/UBSan/TSan gates and the four topologies when changing native shared state.
Choose explicit buffer, operation and queue limits for the device and validate
exhaustion/shutdown behavior. Do not infer a performance advantage from the
language or from a hello-world build.

## Treat WebTTY as a separate application protocol

The native [WebTTY server](../bin/webtty/bin/server/README.md) and
[client](../bin/webtty/bin/client/README.md) build on the same streams but also
manage execution identity, endpoint proofs, payload encryption and process
ownership. An arbitrary Beast WebSocket echo does not implement those contracts.
Start with the server's enrollment, execution-account and authorization choices,
then configure client trust and identity.

C++ WebTTY supports plain and WebSocket transport, not WebTransport. Direct
`Open` sessions differ from engine-managed attach/control capabilities. Local
bearer authentication, registered engine authentication and endpoint-authenticated
E2E payload protection are distinct surfaces; keep the supported policy of each.
Use the Go reference CLI for the higher-level workspace/device provisioning
workflow. Keep the cross-language vectors and native process cleanup/security
gates in the engineering guide when modifying that code.
