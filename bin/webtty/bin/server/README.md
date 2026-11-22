# Native WebTTY server

The native server runs commands under a local OS account. Start with the
[loopback walkthrough](../../../../docs/013-webtty.md), then configure
the listener, endpoint trust and execution account for your deployment.

## Choose a listener

| Mode | Select it with |
| --- | --- |
| Direct | `--uri` provides a TCP or other supported listener address. Use a bearer token for direct WebSocket. Set `--allow-unauthenticated` only for isolated local tests. |
| Lightweight tunnel | `--rstream` creates the tunnel and advertises its WebTTY paths and transport. Use `--no-publish` for private access or `--publish` for public WebSocket. Lightweight plain WebTTY uses a private bytestream. |
| Registered | `--server-id` selects `~/.rstream/webtty/enrollments/<server-id>.yaml`. Use `--server-enrollment` to give the full path. The Engine authenticates registered traffic. |

The server supports plain and WebSocket. It advertises
`rstream.webtty.transport` so clients can choose the matching transport.
Use the Go server for WebTransport.

To require a bearer token on direct WebSocket, provide a file containing the
token or inject it as `RSTREAM_WEBTTY_AUTH_TOKEN`. Plain WebTTY has
no HTTP bearer layer.

```sh
rstream-webtty-server --uri 127.0.0.1:6002 --transport websocket --auth-token-file ./webtty.token
```

## Set endpoint trust

| Option or variable | Purpose |
| --- | --- |
| `--identity` | Select a named identity in `~/.rstream/webtty/identities`. |
| `--identity-file`, `RSTREAM_WEBTTY_IDENTITY_FILE` | Load an endpoint identity JSON file. |
| `RSTREAM_WEBTTY_IDENTITY` | Supply the endpoint identity as inline JSON. |
| `--e2e` | Require encrypted terminal payloads and signed client proof with the selected or default identity. |

Selecting an identity enables end-to-end encryption and requires client proof.
For explicit-key trust, use
[endpoint identity setup](https://rstream.io/docs/webtty/end-to-end-encryption).
The default authorized-client file for an identity is
`~/.rstream/webtty/authorized_clients/<server-identity>.json`.
The server reads it when a session opens. For a provisioned service, use
`--authorized-clients-file` or `RSTREAM_WEBTTY_AUTHORIZED_CLIENTS_FILE`
for a JSON file. Use `--authorized-client-key` or
`RSTREAM_WEBTTY_AUTHORIZED_CLIENT_KEYS` to supply explicit client keys.

For a workspace-managed server, use [device approval](https://rstream.io/docs/webtty/registered-servers).
The enrollment provides the trust material to verify each client's workspace
credential. Use that trust path for workspace-managed clients.

## Choose the execution account

Use `spawn` for a child process under the server's account. Registered
servers default to `login`. Set `--login-user` to an existing OS
account on the server. Run `id -un` on macOS or Linux or read
`$env:USERNAME` on Windows to check the account. Use
`--allow-client-user` when callers are permitted to supply the account.

On POSIX, switching to a different user needs the corresponding service
privileges. On Windows, login mode uses the server's account. Login sessions
have a restricted environment and do not inherit forwarding credentials by
default.

## Run a persistent server

Use `--webtty-config` for an operator-managed YAML file. Both runtime and
enrollment files contain one document. In runtime YAML, `server.listen`
sets the native listener URI. The C++ parser rejects Go-specific
`server.host` and `filesystem` settings.

Direct endpoints serve their own sessions. The Engine coordinates managed
reader attachment and control transfer. Native termination acts on the
session's Unix process group or Windows Job Object. On Unix, descendants
that leave the process group are outside that cleanup. Stopping one session
leaves other sessions available.
