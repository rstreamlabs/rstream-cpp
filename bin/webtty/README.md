# Native WebTTY engineering

The [WebTTY guide](../../docs/013-webtty.md) covers installation, server
trust and operation. This reference defines process and I/O ownership for
changes to the native implementation.

## Unix process ownership

WebTTY Unix launches own their descriptor-inheritance setup independently;
Boost.Process's mutable `limit_handles` property must not be shared between
concurrent launches. On macOS, a process group containing only exited members can
return `EPERM` to a termination request. Cleanup verifies one snapshot of at
most 256 members, including the unreaped leader, before accepting that result.
Members must be zombies or have the kernel's irreversible `P_WEXIT` exit flag;
Darwin can stop delivering signals before an exiting member becomes a zombie.
Live members, incomplete snapshots, or query failures preserve the error.
The leader's PID stays reserved until group cleanup finishes and its native
exit status is collected.

## Windows synchronous pipe ownership

Windows synchronous pipe workers stop accepting operations before shutdown.
Cancellation is retried while waiting for the dedicated worker to exit, because
an initial `CancelSynchronousIo` can arrive before `ReadFile` or `WriteFile`.
Pipe handles stay open until the worker is joined; `CloseHandle` with a pending
synchronous read can block. The retry is confined to shutdown and waits on the
thread handle, so completion wakes it immediately; active I/O gains no polling,
thread, allocation or lock. Deterministic tests hold each read/write behind an
event until the first cancellation has missed, then require cancellation and
preservation of both pipe handles. This applies to ConPTY and the common Windows
console/pipe adapter.

## Darwin WebTTY stdin FIFOs

WebTTY reads FIFO stdin through a single-request `select` worker on Darwin.
Regression tests reproduce missed `kqueue` read notifications and missed `poll`
EOF notifications for named FIFOs. A concurrent writer close can also leave
`select` waiting without an EOF notification. Its idle wait is bounded to one
second so the next nonblocking read observes EOF even if readiness is lost.
Data and cancellation still wake immediately; this adds at most one idle check
per second and no extra worker or payload queue. One borrowed buffer stays alive
through completion, and the next read follows network write completion. A wakeup
pipe cancels a pending wait before the input descriptor is closed. Closing joins the worker; it never waits for stdin EOF. The network
reactor, ordinary files and interactive terminals retain their existing paths.
This fallback consumes one worker and two wakeup descriptors per FIFO client;
select descriptor bounds are checked before use. Associated completion execution
is preserved on the client strand. Native, ASan/UBSan and TSan tests cover bulk
transfer, pending destruction, EOF, repeated close and descriptor bounds.
A deterministic fault-injection test suppresses FIFO readiness while preserving
the real cancellation descriptor, and requires EOF without the test deadline
having to cancel the reader.

## Session protocols

The native E2E suite uses AES-256-GCM for terminal data, X25519 HPKE to wrap
session keys and P-256 ECDSA for endpoint proofs. Use the shared Go,
JavaScript and C++ vectors when changing negotiation or proof handling.
A direct native server handles its own session. Managed attach and control
are coordinated by the Engine.

Keep at most one explicit termination write in flight. Cancellation
before a write leaves the reader connected. If a cancellation may interrupt
a protocol frame, close the transport and let its completions drain. The
termination request completion reports the write outcome. The execution
completion reports the process's native exit status.
