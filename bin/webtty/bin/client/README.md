# rstream-webtty-client

`rstream-webtty-client` connects to WebTTY servers backed by the C++ WebTTY implementation.

Supported transports are `plain` and `websocket`:

```bash
rstream-webtty-client --uri 127.0.0.1:6002 --transport plain -- whoami
rstream-webtty-client --uri 127.0.0.1:6002 --transport websocket --auth-token-file ./webtty.token -- whoami
```

Use `--auth-token-file` or `RSTREAM_WEBTTY_AUTH_TOKEN` when connecting to a local WebSocket server protected by a bearer token. The plain transport has no HTTP auth layer and cannot use bearer tokens.

Providing `--known-server`, `--known-server-key`, `--known-servers-file`, `RSTREAM_WEBTTY_KNOWN_SERVER_KEY`, `RSTREAM_WEBTTY_KNOWN_SERVERS_FILE`, or a default `~/.rstream/webtty/known_servers.json` file implies end-to-end encrypted terminal payloads. Use `--e2e` when the client should fail closed if no known server identity can be resolved. The production form is an endpoint identity: `encryption_key_id:encryption_public_key:signing_key_id:signing_public_key`. It lets the client encrypt a session key for the server and verify the server's signed `ServerHello` before terminal content is sent.

Use `--known-server <name>` when the local known-server file contains several servers and the URI cannot identify the intended entry by host. The selector reads `~/.rstream/webtty/known_servers.json`, or the file passed with `--known-servers-file`, and never calls the control plane.

Use `--identity`, `--identity-file`, `RSTREAM_WEBTTY_IDENTITY`, or `RSTREAM_WEBTTY_IDENTITY_FILE` when the server requires a signed client proof. The client also reads target-scoped `client_identity` associations from `~/.rstream/webtty/known_servers.json` and loads the matching local identity from `~/.rstream/webtty/identities/<name>.identity.json`. Explicit identity flags and environment variables override the known-server association. If authenticated E2E is required and no client identity can be resolved, the client fails before opening the terminal.

The C++ client supports explicit-key WebTTY E2E and the protocol-level client credential field used by workspace-managed sessions. The standalone C++ CLI does not call the control plane; workspace-managed resolution is performed by the rstream CLI and `rstream ui`, which load trusted workspace devices from `~/.rstream/workspaces/<workspace-id>/devices/`. When a credential is produced by another trusted workflow, pass it with `--client-credential-file` or `RSTREAM_WEBTTY_CLIENT_CREDENTIAL_FILE` together with the matching client endpoint identity.
