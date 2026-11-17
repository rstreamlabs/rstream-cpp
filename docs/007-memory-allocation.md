# Memory allocation

C++ applications can supply allocation at two levels: an allocator for SDK-owned
objects and an Asio associated allocator for a completion handler. Choose each
according to the lifetime of the storage it owns.

| Allocation                    | Selected through                                                          | Owns                                                      |
| ----------------------------- | ------------------------------------------------------------------------- | --------------------------------------------------------- |
| SDK object allocation         | A shared `core::allocator::ptr` passed to a client or socket constructor. | Internal state allocated through that object's allocator. |
| Handler-associated allocation | `asio::bind_allocator` on the completion token.                           | Operation storage using the handler's Asio association.   |
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
Build the checkout and run `rstream-example-core-allocator` to inspect that flow.

## Supply an associated allocator

Use `asio::bind_allocator(allocator, handler)` when an operation should use an
application allocator satisfying the standard allocator requirements. This is
a distinct interface from `core::allocator`; its storage may have a shorter
lifetime than the client.

Move-only handlers retain their associations. When a type-erased completion
handler borrows allocator storage from the handler itself, an operation control
block may outlive that storage through cancellation state. The SDK uses its
owning fallback for that control block while preserving the handler's
associations for intermediate I/O and completion.

Applications implementing composed operations must make the same lifetime
choice. The [Asio engineering contract](014-sdk-engineering.md#boostasio-contract)
describes the owning fallback and the late-cancellation/concurrency tests
required when changing it. Measure allocation counts for the actual workload
before sizing a pool; buffer, operation and queue limits also contribute to
peak memory use.
