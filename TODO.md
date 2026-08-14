# Follow-up work

## C++ TLS and asynchronous lifecycle hardening

The focused multithreaded TLS half-close test passes with the quality, AddressSanitizer, and ThreadSanitizer builds as of 2026-08-13. The demonstrated memory-safety defect was an allocator-lifetime issue at type-erased asynchronous-operation boundaries, not an OpenSSL or TLS defect. The affected boundaries have dedicated fixes and regression tests in the current worktree.

This area is deliberately not the critical path of the current guide and sample validation. Before declaring the TLS lifecycle audit exhaustive, complete the following follow-up matrix:

- Exercise TLS 1.2 and TLS 1.3 handshake, cancellation, half-close, peer-close, timeout, and abrupt-reset paths with one and several `io_context` worker threads.
- Repeat client/server interoperability in both directions for Go and C++, including concurrent streams and shutdown during backpressure.
- Run long-duration and high-concurrency stress tests under AddressSanitizer and ThreadSanitizer, and retain machine-readable evidence in CI.
- Add the corresponding stateful associated-allocator abandonment checks to every remaining type-erased operation boundary, including Windows-only handles in Windows CI.
- Remove the stale OpenSSL 1.1 linker search path emitted by macOS builds after verifying that packaging remains compatible with the supported OpenSSL versions.

Completion requires zero sanitizer findings, deterministic cancellation, exactly-once completion, no handler after owner destruction, bounded shutdown time, and no measurable throughput or latency regression against the recorded baseline.
