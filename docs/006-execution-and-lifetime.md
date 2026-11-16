# Execution and object lifetime

An rstream client or socket receives the application's executor. Its operations
run with that event loop, so the application decides how many threads execute
it and how sessions share state.

## Event loops and completion executors

A coroutine can obtain its executor through `co_await asio::this_coro::executor`.
Callback code can use an `io_context` or a session strand. A completion handler
can select its associated executor with `asio::bind_executor`:

```cpp
client.async_connect(asio::bind_executor(session_strand,
    [owner](const boost::system::error_code& error) {
      owner->on_connected(error);
    }));
```

Here `owner` retains the session state and `client` through completion. The
[callback program](../example/io-rstrm/callback.cpp) shows a complete shared
owner. Keep filesystem/device I/O and future waits on workers separate from
the event loop so other connections can progress.

## Shared state and buffers

| Object               | Lifetime and concurrency                                                                             |
| -------------------- | ---------------------------------------------------------------------------------------------------- |
| Client and tunnel    | Keep them alive while creating tunnels or accepting streams; close their owned work during shutdown. |
| Direct socket copies | Copies share the same connection. Closing a copy affects the other handles.                          |
| Read/write buffers   | Retain storage until the operation's completion, including cancellation completion.                  |
| Session state        | Serialize access through its executor/strand when several threads run the loop.                      |
| Beast stream         | Serialize its operations according to Beast's contract, alongside the session state.                 |

Handlers may be move-only. SDK operations preserve their executor, allocator
and cancellation associations. See [memory allocation](007-memory-allocation.md)
for allocating operation state and [cancellation](008-cancellation-and-recovery.md)
for the completion point that releases borrowed buffers.
