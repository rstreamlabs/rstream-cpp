# SDK runtime engineering

## Constrained-system contract

Runtime behavior must be explicit and bounded:

- queues, buffers, retries, pending operations, and worker counts have limits;
- the SDK does not create hidden worker threads;
- blocking work does not run on Asio event-loop threads;
- hot paths do not perform avoidable filesystem, process, or network I/O;
- object ownership and shutdown order remain deterministic;
- cancellation and destruction release work and resources promptly;
- malformed or hostile input cannot cause unbounded allocation or retry loops;
- logs and errors provide actionable context without exposing credentials.

New background work, caches, locks, queues, or allocations require a stated
reason and tests for exhaustion, cancellation, and shutdown. Performance work
must be measured; it must not trade correctness for an assumed optimization.

## Boost.Asio contract

Every asynchronous public operation behaves as a regular Asio operation:

- use `async_initiate` or an equivalent composed-operation mechanism;
- preserve the associated executor, allocator, cancellation slot, and
  move-only completion handler;
- complete exactly once on success, failure, cancellation, and destruction;
- document and test immediate versus deferred completion when observable;
- retain outstanding work only for the lifetime of the operation;
- serialize mutable asynchronous state through its executor or strand;
- use mutexes only for state also accessed synchronously or across executors;
- never invoke an external callback while holding a lock;
- never depend on sleeps for operation ordering in tests.

Tests must cover overlapping operations, cancellation before and during I/O,
partial I/O, peer failure, reconnect, close, destruction, and handler
reentrancy where applicable. ThreadSanitizer and repeated lifecycle tests are
acceptance gates for changes to shared state.

An `any_completion_handler` allocator borrows storage owned by the handler.
It cannot allocate a shared operation control block that can outlive completion,
including blocks retained by cancellation weak pointers or a pending timer.
Those operations use the object's owning allocator. Templated operations use
`core::shared_operation_allocator`: ordinary custom allocator associations are
preserved, while an erased allocator uses the owning fallback. Intermediate I/O
and completion still preserve executor, allocator and cancellation associations.
This does not add an allocation, thread or lock; it changes ownership of the
existing control block. Tests must also exercise completion followed by late
cancellation and concurrent completion on another executor.

## Finishing one side of a stream

After the last write completes, call `async_shutdown_send` to finish sending
on a connected TCP or TLS stream. Keep reading to receive the peer's reply.
On TLS, completion means the local close alert has been written. The peer's
close alert or a transport error completes the remaining read. A transport
plugin without a send-shutdown operation reports `operation_not_supported`.

## rstream runtime contract

The SDK must preserve the rstream contract from configuration input to runtime
behavior:

1. Parse SDK options, URI parameters, environment variables, the selected
   context, the configuration file, and defaults.
2. Apply the documented precedence without silently ignoring a supported
   value.
3. Normalize values and reject invalid combinations, unsupported features,
   and conflicting authentication modes.
4. Produce an immutable effective configuration with secrets redacted from
   errors and logs.
5. Map the effective configuration to internal state and protobuf fields
   without changing its meaning.
6. Preserve engine error semantics, capability checks, cancellation, retry,
   reconnect, and shutdown behavior.

Configuration changes require positive, negative, precedence, wire-mapping,
and runtime tests. Shared configuration behavior must remain aligned with the
Go reference SDK. Unsupported rstream features must fail explicitly rather
than fall back to a different behavior.

For native WebTTY process and pipe ownership, see [WebTTY engineering](../../bin/webtty/README.md).
