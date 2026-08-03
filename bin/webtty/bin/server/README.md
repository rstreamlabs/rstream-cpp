# rstream-webtty-server

`rstream-webtty-server` exposes a WebTTY server backed by the C++ WebTTY implementation.

Use `--rstream` to serve WebTTY through an rstream tunnel URI. The generated URI includes the standard WebTTY discovery labels. Use `--publish` for a published tunnel and `--no-publish` for a private tunnel. Use `--uri` for a direct local listener. The C++ CLI keeps the SDK URI model visible: `--uri` is passed to `rstream::io::address` and may represent a TCP listener or an rstream endpoint depending on the URI scheme.

```text
application-protocol=rstream.webtty
rstream.webtty.capabilities=exec
rstream.webtty.exec.path=/
```

Use `--identity` for a named local identity under `~/.rstream/webtty/identities`, `--identity-file` for an explicit path, `RSTREAM_WEBTTY_IDENTITY` for an inline endpoint identity JSON document, or `RSTREAM_WEBTTY_IDENTITY_FILE` in service-manager environments.

Supported transports are `plain` and `websocket`:

```bash
rstream-webtty-server -v --uri 127.0.0.1:6002 --transport plain --allow-unauthenticated
rstream-webtty-server -v --uri 127.0.0.1:6002 --transport websocket --auth-token-file ./webtty.token
```

Local WebSocket servers require a bearer token from `--auth-token-file` or `RSTREAM_WEBTTY_AUTH_TOKEN` unless `--allow-unauthenticated` is set for isolated development. The plain transport has no HTTP auth layer and therefore also requires `--allow-unauthenticated` for local direct use. Registered servers rely on rstream Engine authentication instead of this local bearer-token surface.

Providing `--identity`, `--identity-file`, `RSTREAM_WEBTTY_IDENTITY`, or `RSTREAM_WEBTTY_IDENTITY_FILE` implies encrypted terminal payloads and signed client proof. Use `--e2e` when the server should require E2E while loading the default local identity. Authorize explicit-key clients with `rstream webtty authorized-client add <name> --identity <server-identity> --key <client-endpoint-identity>`, which writes `~/.rstream/webtty/authorized_clients/<server-identity>.json`. The server reads that file when a new session opens, so newly added or removed clients affect new sessions without restarting the server. Use `--authorized-client-key`, `RSTREAM_WEBTTY_AUTHORIZED_CLIENT_KEYS`, `--authorized-clients-file`, or `RSTREAM_WEBTTY_AUTHORIZED_CLIENTS_FILE` for CI, containers, and service-manager deployments that need an explicit authorization source. `--server-id` loads `~/.rstream/webtty/enrollments/<server-id>.yaml`; `--server-enrollment` loads an explicit enrollment file; `--webtty-config` loads an operator-managed runtime config. In that config, `server.listen` maps to the C++ URI model. Local WebTTY identities and enrollment files use the same `~/.rstream/webtty` layout as the rstream CLI.

Workspace-managed E2E is driven by trusted workspace devices. The enrollment file contains the workspace trust pins required to verify signed client credentials locally. At runtime, the C++ server verifies the credential embedded in `ClientProof`; it does not call the control plane. Do not configure explicit authorized-client keys for a workspace-managed server.

Execution modes are `spawn` and `login`. Registered servers default to `login`; set `--login-user <username>` to the name of the existing local OS account that will own every session. This is not an rstream account or the connecting operator's username. Use `--allow-client-user` only when clients are deliberately allowed to select the OS user, or `--execution-mode spawn` for the lightweight child-process model. C++ login mode does not handle passwords. On POSIX it applies the target user, primary group, and supplementary groups through the local process credentials, which requires suitable service privileges. On Windows, login mode accepts the same account that runs the server and rejects attempts to switch accounts. Login sessions receive a conservative administrative environment and do not automatically inherit SSH agent sockets, cloud credentials, or rstream tokens. WebTransport is not implemented in the C++ server; use the Go server for WebTransport.
