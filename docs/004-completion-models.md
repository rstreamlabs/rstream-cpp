# Completion handlers, futures and coroutines

Boost.Asio completion tokens let the same SDK operation fit three control
flows. Each example connects, creates one private bytestream tunnel, prints
its endpoint and closes its tunnel and client.

[Configure the checkout and the example path](../example/io-rstrm/README.md#prepare-the-example-binaries).
Build all three targets from the repository root.

```sh
cmake --build --preset conan-debug --target rstream-example-callback rstream-example-future rstream-example-coroutine
```

Use a [context](002-configuration.md)
for a direct CE Engine or a hosted project that permits private tunnels.
The identity must be able to create a tunnel. The examples use the same
properties and require the same permission.

```sh
rstream-example-callback
rstream-example-future
rstream-example-coroutine
```

| Style              | Program and binary                                                                | Best fit                                                                                                                                             |
| ------------------ | --------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------- |
| Completion handler | [`callback.cpp`](../example/io-rstrm/callback.cpp), `rstream-example-callback`    | Existing callback APIs and explicit asynchronous state machines. A shared owner retains the client until completion.                                 |
| Future/promise     | [`future.cpp`](../example/io-rstrm/future.cpp), `rstream-example-future`          | A synchronous orchestration thread waiting for work performed by a separate event-loop thread. Asio's `use_future` supplies the promise/future pair. |
| Coroutine          | [`coroutine.cpp`](../example/io-rstrm/coroutine.cpp), `rstream-example-coroutine` | Sequential asynchronous code with exception propagation and locals retained in the coroutine frame.                                                  |

Each prints `Private tunnel: ...` and exits with zero on success. That
endpoint has closed by the time the program returns. Engine
rejection produces a diagnostic and nonzero status. The snippets isolate how each style creates the tunnel. The complete programs also connect and close their resources.

## Completion handler

```cpp
self->m_client.async_create_tunnel(properties,
    [self](const boost::system::error_code& error, rstrm::tunnel tunnel) {
      if (!error) {
        std::cout << "Private tunnel: " << tunnel.local_endpoint() << std::endl;
        tunnel.close();
      }
      self->finish(error);
    });
```

The handler closes the tunnel on success and finishes with the same error code.
The callback program
retains one shared application object through its handlers and runs its
`io_context` on the main thread. This makes the transitions and lifetime
explicit at each callback.

## Future/promise

```cpp
auto tunnel = client.async_create_tunnel(properties, asio::use_future).get();
```

The program starts an event-loop worker before waiting on the future. `get()`
waits on the orchestration thread and reports failure by exception. Keep the
worker alive with a work guard; release the guard and join the worker after
closing the client. Calling `get()` on the only event-loop thread would prevent
completion.

## Coroutine

```cpp
auto tunnel = co_await client.async_create_tunnel(properties, asio::use_awaitable);
```

`co_await` suspends this coroutine while the event loop continues processing
other work. The program starts it with `co_spawn`, observes its exception in
the completion handler and closes the control client on both success and
failure. The same main thread runs the event loop.

## Continue with application I/O

Completion style and interface choice are independent: clients, direct sockets,
acceptors and URI streams use the same Asio associations. Choose the style that
fits the surrounding application. See [execution and lifetime](006-execution-and-lifetime.md),
[associated allocation](007-memory-allocation.md) and
[cancellation and recovery](008-cancellation-and-recovery.md) for the contracts
shared by all three styles. The [private echo](003-tunnel-patterns.md#private-accept-and-dial)
adds concurrent accept/dial and bounded read/write exchanges as a separate example.
