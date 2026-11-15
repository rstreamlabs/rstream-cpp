# C++ SDK configuration

The SDK reads the rstream CLI’s configuration and selected context, by default from `~/.rstream/config.yaml`. Applications and devices can also receive an explicit engine address and credentials without an interactive login.

## Select a project on a developer machine

```sh
rstream login
rstream project use my-project-endpoint
```

Replace the endpoint with your project’s endpoint. The second command selects the default context. The [CLI workflow](https://github.com/rstreamlabs/rstream-go/blob/main/docs/002-cli-workflow.md) explains project-scoped credentials and other authentication methods.

## Environment variables

| Variable | Purpose |
| --- | --- |
| `RSTREAM_CONFIG` | Configuration file path; defaults to `~/.rstream/config.yaml`. |
| `RSTREAM_CONTEXT` | Context to select from the file. |
| `RSTREAM_API_URL` | Control plane API URL for context resolution. |
| `RSTREAM_ENGINE_ADDRESS` | Highest-priority engine address override. |
| `RSTREAM_ENGINE` | Engine override when `RSTREAM_ENGINE_ADDRESS` is absent; `host:port` selects the default TLS URI and ALPN. |
| `RSTREAM_AUTHENTICATION_TOKEN` | Token override when token behavior is not fixed in SDK options. |
| `RSTREAM_MTLS_CERT_FILE` | Client certificate file for mTLS authentication. |
| `RSTREAM_MTLS_KEY_FILE` | Client private key file for mTLS authentication. |
| `RSTREAM_TUNNEL_TRANSPORT` | Engine transport selector: `auto`, `tls` or `quic`. |
| `RSTREAM_QUIC_TRANSPORT` | Legacy transport selector; prefer `RSTREAM_TUNNEL_TRANSPORT`. |

Engine resolution checks the explicit environment overrides before the selected context. Token resolution uses explicit SDK options, then the environment, then configuration-derived values.

## Trust the engine

The engine TLS connection verifies the certificate chain and the peer identity.
Default SDK builds use the bundled public CA certificates. `SSL_CERT_FILE`
only applies to builds configured with `EMBED_DEFAULT_CA_CERTIFICATES=OFF`;
it does not replace the bundled trust store in the default build.

For a private engine CA, configure the selected context explicitly:

```yaml
defaults:
  context:
    name: private-engine
contexts:
  - name: private-engine
    engine: project.cluster.example:443
    transport:
      tls:
        caFile: /etc/rstream/engine-ca.pem
        serverName: project.cluster.example
```

Keep the agent's token or mTLS identity in the provisioned context or supported
environment variables. `serverName` is the certificate identity to verify; it
must match a DNS SAN. With an explicit TLS URI, use `ssl.cacert_file` and, when
needed, `ssl.sni`, as shown in the [URI reference](006-uri-model.md#tls-stream-options).
IP literals are checked against IP SANs. Keep peer verification enabled.
An explicit engine environment override takes precedence over context TLS
settings; include its CA and identity parameters in that full URI.

A provisioned process can avoid browser login using `RSTREAM_ENGINE` (a
`host:port`, expanded to verified TLS with the rstream ALPN) and
`RSTREAM_AUTHENTICATION_TOKEN`. Supply the real project-scoped token through
its secret manager or service environment. A programmatic client can instead
set `config_client::m_token`, construct the client with that config, and pass a
verified TLS `io::address` to `async_connect`. Configure these before initiating
operations: a client snapshots its default engine and credentials at construction.

## Authenticate an agent

Token authentication and mTLS are mutually exclusive for the agent connection. Setting the certificate and key environment variables selects mTLS and excludes configuration-derived tokens. Combining those variables with `RSTREAM_AUTHENTICATION_TOKEN` is an error.

A provisioned context can use certificate files:

```yaml
contexts:
  - name: prod-mtls
    engine: project.cluster.example:443
    auth:
      mtls:
        certificateFile: /etc/rstream/client.pem
        keyFile: /etc/rstream/client-key.pem
```

Inline `certificate` and `key` values are also supported for generated configurations.

## Transport boundaries

| Configuration | C++ behavior |
| --- | --- |
| `transport.mode: auto` or `tls` | Uses TLS. |
| `transport.mode: quic`, an invalid mode, or legacy `transport.useQuic: true` | Fails during resolution. |
| `transport.proxy`, including HTTP CONNECT, SOCKS5 or environment-derived proxying | The shared schema is parsed, but a selected configuration requesting a proxy fails because the engine connection does not implement it. |
| `auth.mtls.storage.kind: exec` | Returns `unsupported_mtls_exec` before connecting and does not launch the helper. Use the Go SDK or CLI for this storage backend. |

Unsupported settings in a selected context do not prevent using other supported contexts in the same configuration file. Protocol capabilities are summarized in the [SDK overview](../README.md).
