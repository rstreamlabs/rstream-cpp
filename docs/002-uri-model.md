# URI Model in rstream-cpp

`rstream-cpp` uses URI query parameters as a primary configuration surface for tunnel properties, transport behavior, and TLS settings.

This document introduces the URI model used by the SDK and points to the code paths that parse each namespace.

## Why URI Parameters Matter

URIs are used across the SDK and tools to carry:

- Tunnel publication and policy options
- Client behavior options
- Transport-level options (`tcp`, `unix`, `serial`)
- TLS transport configuration (`ssl.*`)

The same URI model is reused in SDK APIs and in binaries such as `rstream-tunnel`, `rstream-webtty-*`, and `rstream-nperf-*`.

## Parameter Namespaces

### `rstrm.*` (Tunnel Properties)

Used for tunnel properties and publication controls.

Common keys include:

- `rstrm.type`
- `rstrm.publish`
- `rstrm.protocol`
- `rstrm.labels` (repeatable, key/value encoded as `k=v`)
- `rstrm.geoip`
- `rstrm.trusted_ips`
- `rstrm.hostname` (Stable domain)
- `rstrm.host` (deprecated read-only field returned by the server)
- `rstrm.tls_mode`
- `rstrm.tls_alpns`
- `rstrm.tls_min_version`
- `rstrm.tls_ciphers`
- `rstrm.mtls_auth`
- `rstrm.http_version`
- `rstrm.upstream_tls`
- `rstrm.http_use_tls` (deprecated HTTP-only alias)
- `rstrm.token_auth`
- `rstrm.rstream_auth`
- `rstrm.challenge_mode`

Parsing entry point:

- `lib/rstream/io-rstrm/io-rstrm.cpp` (`parse_tunnel_properties`)

Serialization to rstream tunnel protobuf:

- `lib/rstream/io-rstrm/detail/convert.cpp` (`convert(protobuf::TunnelProperties&, ...)`)

Stable domains use the engine-owned pattern `<slug>-<project-endpoint>.t.<cluster-domain>` and are carried in `rstrm.hostname`.

Note: many policy/auth controls are enforced by the rstream edge. The SDK transports and applies configuration locally where needed, while edge policy decisions remain centralized.

### `rstream.*` (Client/Acceptor Behavior)

Used for SDK-side behavior options.

Common keys include:

- `rstream.no_token`
- `rstream.token`
- `rstream.retry`

Parsing entry points:

- `lib/rstream/io-rstrm/io-rstrm.cpp` (`parse_config`, `parse_settings_acceptor`)

### `ssl.*` (TLS Transport)

Used to configure TLS behavior in the underlying stream layer.

Common keys include:

- `ssl` (flag to enable TLS)
- `ssl.tlsv12`, `ssl.tlsv13`
- `ssl.peer_verification`, `ssl.request_peer_cert`
- `ssl.key`, `ssl.key_file`, `ssl.key_type`, `ssl.passphrase`
- `ssl.cert`, `ssl.cert_file`, `ssl.cert_type`
- `ssl.cacert`, `ssl.cacert_file`, `ssl.capath`
- `ssl.ciphers`, `ssl.ciphers_tlsv13`
- `ssl.sni`, `ssl.alpn_protos`
- `ssl.engine`
- `ssl.max_ongoing_upstream_ops`
- `ssl.async_shutdown_timeout_ms`

Parsing entry point:

- `lib/rstream/io/detail/stream/ssl.cpp` (`parse_ssl_config`)

### Transport-Specific Namespaces

Transport plugin options are parsed in plugin implementations.

`tcp.*` examples:

- `tcp.inet4`, `tcp.inet6`, `tcp.no_resolve`
- `tcp.no_delay`, `tcp.keep_alive`
- `tcp.reuse_address`

`unix.*` examples:

- `unix.abstract`

`serial.*` examples:

- `serial.baudrate`

Parsing entry points:

- `plugin/io-generic/tcp/*.cpp`
- `plugin/io-generic/unix/*.cpp`
- `plugin/io-generic/serial/*.cpp`

## Boolean and Value Parsing Rules

URI values are parsed with helpers in:

- `lib/rstream/io/detail/stream/url.cpp`

Rules:

- Boolean accepts `true` or `false`.
- A present boolean key without explicit value is treated as `true`.
- String/integer keys require explicit values.

## Examples

### 1) Published HTTP tunnel with auth and policy controls

```text
rstrm://?rstrm.publish=true&rstrm.protocol=http&rstrm.token_auth=true&rstrm.challenge_mode=true&rstrm.trusted_ips=203.0.113.0/24,198.51.100.12/32&rstrm.geoip=FR,US&rstrm.labels=env%3Dprod
```

### 2) TLS transport options for engine connectivity

```text
tcp://engine.example:443?ssl&ssl.tlsv13=true&ssl.alpn_protos=rstrm%2F1&ssl.peer_verification=true&ssl.sni=engine.example
```

### 3) Retry and explicit token behavior

```text
tcp://engine.example:443?rstream.retry=true&rstream.no_token=false&rstream.token=TOKEN_VALUE
```

## Notes on Coverage

`io-rstrm` tunnel creation currently enforces bytestream tunnel type in the main client flow.

Reference checks:

- `lib/rstream/io-rstrm/client.cpp` (type validation around `async_create_tunnel` and tunnel response handling)
