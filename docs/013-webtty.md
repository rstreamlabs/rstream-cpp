# WebTTY with the C++ SDK

WebTTY lets an operator open a terminal or run a command on a remote
machine. The native server runs the process under a configured operating
system account. The client carries terminal input and output, and can verify
the server's endpoint identity before sending terminal data.

## Run a local command

Use the client and server from [the native utilities](012-command-line-tools.md#obtain-the-tools)
on the same machine. To build these two executables after configuring the
checkout, run:

```sh
cmake --build --preset conan-debug --target rstream-webtty-client rstream-webtty-server
```

On macOS or Linux, find them in `bin/webtty/bin/client/` and
`bin/webtty/bin/server/` inside the build directory. On Windows with MSVC,
use the executables under the build directory's `bin/`. Run each command
using that executable path or add the appropriate directory to the
terminal's path.

This test binds to loopback, runs under your current account and uses direct
plain WebTTY. Use a shell without WebTTY token or identity overrides.

```sh
rstream-webtty-server --uri 127.0.0.1:6002 --transport plain --allow-unauthenticated
```

In a second terminal, run:

```sh
rstream-webtty-client --uri 127.0.0.1:6002 --transport plain -- whoami
```

The command prints the account running the server. On a host with PTY
support, omit `-- whoami` to open an interactive shell and enter `exit`
to close it. Stop the server with Ctrl+C. Before letting other machines reach it, choose a trusted deployment.

## Choose how the terminal is reached

| Mode | When to use it |
| --- | --- |
| Direct plain or WebSocket | A native client connects to a known listener. Protect direct WebSocket with a bearer token, and configure endpoint identities for end-to-end protection. |
| Lightweight tunnel | Start the server with a configured Engine context. A plain tunnel is private. WebSocket can use private dialing or a published endpoint if the project permits it. |
| Registered server | An authorized operator enrolls the server and approves workspace-managed devices. Use the [registered server workflow](https://rstream.io/docs/webtty/registered-servers). |

The C++ tools support plain and WebSocket connections. Use the reference
Go implementation for WebTransport. Network or Engine admission controls
who can reach the server. WebTTY endpoint trust controls who can exchange
terminal content.

| Role | Reference |
| --- | --- |
| Server | [Listener, authorization and execution account](../bin/webtty/bin/server/README.md). |
| Client | [Transport, trust and client identity](../bin/webtty/bin/client/README.md). |
| SDK contributor | [Process and I/O engineering](../bin/webtty/README.md). |
