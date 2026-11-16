# Completion handlers, futures and coroutines

Asio completion tokens let the same SDK operation fit different control flows.
The three complete programs perform the same operations: connect to the selected engine,
create one private bytestream tunnel, print its endpoint, close the tunnel and
control connection, then exit. They use identical tunnel properties and require
the same project permissions.

Build the [checkout](001-building-from-source.md), select a
[context](002-configuration.md), then run any of the three binaries:

| Style              | Program and binary                                                                | Best fit                                                                                                                                             |
| ------------------ | --------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------- |
| Completion handler | [`callback.cpp`](../example/io-rstrm/callback.cpp), `rstream-example-callback`    | Existing callback APIs and explicit asynchronous state machines. A shared owner retains the client until completion.                                 |
| Future/promise     | [`future.cpp`](../example/io-rstrm/future.cpp), `rstream-example-future`          | A synchronous orchestration thread waiting for work performed by a separate event-loop thread. Asio's `use_future` supplies the promise/future pair. |
| Coroutine          | [`coroutine.cpp`](../example/io-rstrm/coroutine.cpp), `rstream-example-coroutine` | Sequential asynchronous code with exception propagation and locals retained in the coroutine frame.                                                  |

Each prints `Private tunnel: ...` and exits with zero on success. Engine
rejection produces a diagnostic and nonzero status. The snippets below isolate
the same connect operation; the linked programs include creation and cleanup.

## Completion handler

```cpp
client.async_connect([owner](const boost::system::error_code& error) {
  owner->on_connected(error);
});
```

The handler starts the next step after checking `error`. The callback program
retains one shared application object through its handlers and runs its
`io_context` on the main thread. This makes the transitions and lifetime
explicit at each callback.

## Future/promise

```cpp
client.async_connect(asio::use_future).get();
```

The program starts an event-loop worker before waiting on the future. `get()`
waits on the orchestration thread and reports failure by exception. Keep the
worker alive with a work guard; release the guard and join the worker after
closing the client. Calling `get()` on the only event-loop thread would prevent
completion.

## Coroutine

```cpp
co_await client.async_connect(asio::use_awaitable);
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
shared by all three styles. The [private echo](003-integration-model.md#private-accept-and-dial)
adds concurrent accept/dial and bounded read/write exchanges as a separate example.
