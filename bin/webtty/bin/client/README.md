# Native WebTTY client

The client opens an interactive terminal or runs one command. Use the
[WebTTY walkthrough](../../../../docs/013-webtty.md) to install the tools,
choose an endpoint and try a local command.

## Connect to a known endpoint

Use `--uri` to select the direct listener or private `rstrm://`
address. Match the server's transport, `plain` or `websocket`.
A direct WebSocket server can require an HTTP bearer token. Provide its file
with `--auth-token-file` or inject `RSTREAM_WEBTTY_AUTH_TOKEN`.

```sh
rstream-webtty-client --uri 127.0.0.1:6002 --transport websocket --auth-token-file ./webtty.token -- whoami
```

Plain WebTTY has no HTTP bearer layer. For a native local terminal, use a
server and client configured for the same transport.

## Verify the server identity

| Option or variable | Purpose |
| --- | --- |
| `--known-server` | Select an entry in the local known-server file. |
| `--known-servers-file`, `RSTREAM_WEBTTY_KNOWN_SERVERS_FILE` | Give the path to a known-server file. |
| `--known-server-key`, `RSTREAM_WEBTTY_KNOWN_SERVER_KEY` | Supply the server key or complete endpoint identity. |
| `--e2e` | Require end-to-end encrypted terminal content and fail when no identity can be selected. |

Selecting a known server, key or file requires encryption. The client
consults `~/.rstream/webtty/known_servers.json` by default when
`--known-server` or `--e2e` requests it. Use a full endpoint identity
(`encryption_key_id:encryption_public_key:signing_key_id:signing_public_key`)
to verify the server's signature before sending terminal data. See
[end-to-end encryption](https://rstream.io/docs/webtty/end-to-end-encryption)
for trust enrollment.

To sign client proof, select the local identity with `--identity`,
`--identity-file`, `RSTREAM_WEBTTY_IDENTITY` or
`RSTREAM_WEBTTY_IDENTITY_FILE`. These override the matching
`client_identity` in the known-server entry. A named identity resolves
under `~/.rstream/webtty/identities`.

## Use a registered server

Use the [registered server workflow](https://rstream.io/docs/webtty/registered-servers)
to prepare a workspace device. The standalone C++ client consumes a prepared
credential through `--client-credential-file` or
`RSTREAM_WEBTTY_CLIENT_CREDENTIAL_FILE` with its matching local endpoint
identity. The credential lets the server verify workspace admission and
supports replay for the trusted client. Workspace setup and resolution are
handled by the rstream CLI or dashboard.

## Resolve the transport for a private dial

For a `rstrm://` URL, the client discovers the transport from the
Engine's tunnel inventory. An explicit transport must match that advertised
by the server. Legacy datagram WebTTY entries identify WebTransport, which
requires the Go client. An engine-only device with stream permission and no
inventory access can set `--no-discovery` and `--transport`. Supply the
endpoint trust, local identity and any signed workspace credential for that
path.

Set `RSTREAM_DATA_DIR` to an absolute directory to change where
WebTTY and workspace credentials are stored. Its default is `~/.rstream`.
`RSTREAM_CONFIG` selects the separate CLI and SDK connection configuration.

## Disconnect or stop execution

Closing the local terminal disconnects the client. An SDK caller can request
that a supported native server stop its command through
`async_request_termination`. Its completion confirms the request was sent.
Wait for `async_run` to report the native exit status and receive any final
output. For workspace-managed sessions, use the Engine's session controls
and the required role. See [WebTTY operations](https://rstream.io/docs/webtty/operations).
