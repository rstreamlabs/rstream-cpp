# URI reference

The C++ SDK and its native tools use URIs to describe a connection and its options. The scheme identifies the connection type; query parameters configure the tunnel, rstream client or underlying transport. This is an alternative configuration surface, not a different authorization model. Engine project settings still determine which requests are permitted.

Start with [Configuration](002-configuration.md) for a selected CLI context and credentials. Use this reference when constructing an explicit URI or configuring a native tool.

## Choose the parameter namespace

| Namespace                     | Configures                                                           |
| ----------------------------- | -------------------------------------------------------------------- |
| `rstrm.*`                     | The service exposed by the tunnel and its publication/access policy. |
| `rstream.*`                   | rstream client credentials and acceptor behavior.                    |
| `ssl.*`                       | TLS on the underlying stream connection.                             |
| `tcp.*`, `unix.*`, `serial.*` | The selected transport plugin.                                       |

A URI may contain credentials. Keep it out of logs and version control; use the shared context or supported environment configuration when passing secrets separately is possible.

## Tunnel properties

The native client creates bytestream tunnels. A parameter describing a protocol does not add datagram or QUIC runtime support to this SDK.

| Parameter               | Purpose or constraint                                                                       |
| ----------------------- | ------------------------------------------------------------------------------------------- |
| `rstrm.type`            | Tunnel type; the native client requires bytestream.                                         |
| `rstrm.publish`         | Request a forwarding address accessible to ordinary protocol clients.                       |
| `rstrm.protocol`        | Select the protocol exposed by a published tunnel.                                          |
| `rstrm.labels`          | Repeatable labels encoded as `k=v`; encode a literal equals sign as `%3D` inside the value. |
| `rstrm.geoip`           | Geographic access policy.                                                                   |
| `rstrm.trusted_ips`     | Trusted IP access policy.                                                                   |
| `rstrm.hostname`        | Stable domain requested from the engine.                                                    |
| `rstrm.host`            | Deprecated read-only field returned by the server.                                          |
| `rstrm.tls_mode`        | TLS termination or passthrough mode for the published service.                              |
| `rstrm.tls_alpns`       | Application protocols advertised for the published TLS service.                             |
| `rstrm.tls_min_version` | Minimum TLS version requested for the published service.                                    |
| `rstrm.tls_ciphers`     | Cipher selection for the published TLS service.                                             |
| `rstrm.mtls_auth`       | Require certificate authentication at the published tunnel endpoint.                        |
| `rstrm.http_version`    | HTTP protocol selection for the tunnel.                                                     |
| `rstrm.upstream_tls`    | Use TLS on the application side of the tunnel.                                              |
| `rstrm.http_use_tls`    | Deprecated HTTP-only alias.                                                                 |
| `rstrm.token_auth`      | Token access policy for incoming tunnel requests.                                           |
| `rstrm.rstream_auth`    | rstream identity-provider access policy.                                                    |
| `rstrm.challenge_mode`  | Browser challenge policy.                                                                   |

Stable domains use the engine-owned pattern `<slug>-<project-endpoint>.t.<cluster-domain>`. Tunnel access controls are distinct from the agent credentials used to establish the engine connection.

One generic resolver keeps the transport selected by its first URI.
Create a new resolver when switching schemes.

## Client and acceptor options

| Parameter          | Purpose or constraint                                                                                                          |
| ------------------ | ------------------------------------------------------------------------------------------------------------------------------ |
| `rstream.no_token` | Suppress token selection for the engine connection. It cannot be combined with an explicit `rstream.token`.                    |
| `rstream.token`    | Supply the engine authentication token explicitly. Token and certificate authentication cannot be combined on that connection. |
| `rstream.retry`    | Enable automatic control reconnection (default `true`). Tunnel creation has a separate retry policy.                                                                  |

## TLS stream options

These settings configure the stream's TLS layer, independently of a published tunnel's `rstrm.tls_*` policy.

| Parameter                                                   | Purpose                                                  |
| ----------------------------------------------------------- | -------------------------------------------------------- |
| `ssl`                                                       | Enable TLS on the stream.                                |
| `ssl.tlsv12`, `ssl.tlsv13`                                  | Select supported TLS protocol versions.                  |
| `ssl.peer_verification`                                     | Configure certificate verification of the remote peer.   |
| `ssl.request_peer_cert`                                     | Request a certificate from the connecting peer.          |
| `ssl.key`, `ssl.key_file`, `ssl.key_type`, `ssl.passphrase` | Private key material or file, its format and passphrase. |
| `ssl.cert`, `ssl.cert_file`, `ssl.cert_type`                | Certificate material or file and its format.             |
| `ssl.cacert`, `ssl.cacert_file`, `ssl.capath`               | Trusted CA material, file or directory.                  |
| `ssl.ciphers`, `ssl.ciphers_tlsv13`                         | Cipher selection for the corresponding TLS versions.     |
| `ssl.sni`, `ssl.alpn_protos`                                | TLS server name and application protocol negotiation.    |
| `ssl.engine`                                                | Cryptographic engine selection.                          |
| `ssl.max_ongoing_upstream_ops`                              | Bound concurrent upstream operations in the TLS stream.  |
| `ssl.async_shutdown_timeout_ms`                             | Bound asynchronous TLS shutdown in milliseconds.         |

## Transport plugin options

Only the plugin owning a transport interprets its namespace.

| Parameter                | Purpose                                        |
| ------------------------ | ---------------------------------------------- |
| `tcp.inet4`, `tcp.inet6` | Select the TCP address family.                 |
| `tcp.no_resolve`         | Disable hostname resolution in the TCP plugin. |
| `tcp.no_delay`           | Configure TCP_NODELAY.                         |
| `tcp.keep_alive`         | Configure TCP keepalive.                       |
| `tcp.reuse_address`      | Configure address reuse.                       |
| `unix.abstract`          | Select an abstract Unix socket address.        |
| `serial.baudrate`        | Configure the serial line rate.                |

## Encode values

Boolean values accept `true` or `false`; a present boolean key with no value means `true`. String and integer parameters require explicit values. Percent-encode characters that would otherwise delimit the URI or its query parameters.

## Examples

A published HTTP tunnel with token access policy and explicit network restrictions:

```text
rstrm://?rstrm.publish=true&rstrm.protocol=http&rstrm.token_auth=true&rstrm.challenge_mode=true&rstrm.trusted_ips=203.0.113.0/24,198.51.100.12/32&rstrm.geoip=FR,US&rstrm.labels=env%3Dprod
```

A verified TLS stream to an engine, with its server name and ALPN:

```text
tcp://engine.example:443?ssl&ssl.tlsv13=true&ssl.alpn_protos=rstrm%2F1&ssl.peer_verification=true&ssl.sni=engine.example
```

That `tcp://` URI is an **engine connection address**. It does not create a
published tunnel when passed to the generic stream acceptor: the scheme selects
the TCP plugin. Use a `rstrm://` URI for a tunnel listener, with the engine and
credentials supplied by the selected context:

```text
rstrm://?rstrm.publish=true&rstrm.protocol=http&rstream.retry=true
```

A private dial selects the tunnel name or ID in the host, for example
`rstrm://echo`. The [integration guide](003-tunnel-patterns.md) shows how the
URI model differs from a direct `client`/`tunnel`/`socket` API.

The optional `server=` query value overrides the engine address and must be
percent-encoded as a nested URI. A dial or listener with this override
requires an explicit `rstream.token` for a token-authenticated Engine.
Use `rstream.no_token` only when the selected Engine allows that connection
without a token. For mTLS, select `rstream.no_token` and configure the
client certificate, key and trust for the explicit Engine. A URI-supplied
server never receives the selected context's implicit token. Use a provisioned context to keep credentials outside the URI.

## HTTP CORS

A published HTTP tunnel can carry an optional `http_cors` policy. The C++ API uses `tunnel_properties::m_http_cors` (`boost::optional<http_cors>`), with `m_allowed_origins`, `m_allowed_methods`, `m_allowed_headers`, `m_exposed_headers`, `m_allow_credentials` and `m_max_age` (seconds).

URI parameters use `rstrm.http_cors.allowed_origins`, `.allowed_methods`, `.allowed_headers`, `.exposed_headers` (comma-separated lists), `.allow_credentials` and `.max_age`. Origins must be explicit HTTP(S) origins without paths or wildcards. The absent policy preserves upstream CORS. Engine validates the complete policy and keeps authentication on all actual operations; preflights do not open upstream streams.

The native CLI accepts the same options and defaults as the Go CLI:

| Option                          | Meaning                                                                                                                |
| ------------------------------- | ---------------------------------------------------------------------------------------------------------------------- |
| `--cors-allow-origin <origin>`  | Enable CORS for an explicit HTTP(S) origin; repeat for each origin. No wildcard, path, credentials, query or fragment. |
| `--cors-allow-method <method>`  | Repeatable allowed method. Default: GET, HEAD, POST.                                                                   |
| `--cors-allow-header <header>`  | Repeatable allowed request header. Default: Authorization, Content-Type.                                               |
| `--cors-expose-header <header>` | Repeatable response header made readable by browser scripts. Default: none beyond browser defaults.                    |
| `--cors-allow-credentials`      | Allow browser-managed credentials for the listed origins. Default: false.                                              |
| `--cors-max-age <seconds>`      | Preflight cache lifetime, 0–86400 seconds. Default: 0.                                                                 |

```sh
rstream-tunnel 8080 --http --token-auth \
  --cors-allow-origin=https://dashboard.example \
  --cors-allow-method=GET --cors-allow-method=PUT
```

## Parser reference

| Surface                                | Implementation                                               |
| -------------------------------------- | ------------------------------------------------------------ |
| Tunnel, client and acceptor parameters | [`io-rstrm.cpp`](../lib/rstream/io-rstrm/io-rstrm.cpp).      |
| Tunnel protocol conversion             | [`convert.cpp`](../lib/rstream/io-rstrm/detail/convert.cpp). |
| TLS stream parameters                  | [`ssl.cpp`](../lib/rstream/io/detail/stream/ssl.cpp).        |
| Boolean and scalar parsing             | [`url.cpp`](../lib/rstream/io/detail/stream/url.cpp).        |
| Transport plugins                      | [`io-generic`](../plugin/io-generic).                        |
