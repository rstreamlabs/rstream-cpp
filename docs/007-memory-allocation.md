# Memory allocation

C++ applications can supply an allocator for SDK-owned objects and an Asio
associated allocator for a completion handler. Choose each
according to the lifetime of the storage it owns.

| Allocation                    | Selected through                                                          | Owns                                                      |
| ----------------------------- | ------------------------------------------------------------------------- | --------------------------------------------------------- |
| SDK object allocation         | A shared `core::allocator::ptr` passed to a client or socket constructor. | Internal state allocated through that object's allocator. |
| Handler-associated allocation | `asio::bind_allocator` on the completion token.                           | Storage selected by the handler's association for intermediate I/O and completion.   |
| Application buffers           | The application's containers or buffer pool.                              | Payload bytes retained until read/write completion.       |

## Supply an object allocator

Implement `core::allocator::allocate(std::size_t)` and `deallocate(void*)`.
`core::allocator::ptr` is a shared owner, so the allocator can remain alive with
SDK state that retains it. With an existing allocator instance:

```cpp
rstream::core::allocator::ptr allocator = application_allocator;
rstrm::client client(executor, allocator);
rstrm::socket socket(executor, allocator);
```

The allocator must return valid suitably aligned storage, report exhaustion
with an allocation failure and support calls from the executors that use it.
Keep any backing pool alive through the last allocation's release. The complete
[allocator example](../example/core/common/example_core_allocator.cpp) implements
the interface and traces allocation/deallocation around an owned memory block.
Build the [native checkout](001-building-from-source.md) with the target
`rstream-example-core-allocator`. On macOS or Linux it is under
`example/core/common/` in the build directory. On Windows use the executable
in the build directory's `bin/`.

## Supply an associated allocator

Use `asio::bind_allocator(allocator, handler)` when an operation should use an
application allocator satisfying the standard allocator requirements. This is
a distinct interface from `core::allocator`; its storage may have a shorter
lifetime than the client.

For an asynchronous connection using an application-provided allocator and
completion handler:

```cpp
client.async_connect(asio::bind_allocator(operation_allocator, std::move(on_connect)));
```

The client uses its object allocator for retained connection and tunnel-creation
state. It preserves the handler's associated allocator for intermediate I/O
and completion. Keep any pool used by the allocator alive through the operations
that use it. See the [Asio engineering contract](../lib/rstream/README.md#boostasio-contract)
for erased completion handlers and the fallback used when retained operation
state needs longer-lived storage.
