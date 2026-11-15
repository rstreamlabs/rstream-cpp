# C++ SDK configuration

The SDK reads the rstream CLI’s configuration and selected context, by default from `~/.rstream/config.yaml`. Applications and devices can also receive an explicit engine address and credentials without an interactive login.

## Select a project on a developer machine

```sh
rstream login
rstream project use my-project-endpoint
```

Replace the endpoint with your project’s endpoint. The second command selects the default context. The [CLI workflow](https://github.com/rstreamlabs/rstream-go/blob/main/docs/001-cli-workflow.md) explains project-scoped credentials and other authentication methods.

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

The TLS connection verifies the engine’s certificate and hostname. The OpenSSL backend uses its default trust paths. For a private CA, set `SSL_CERT_FILE` to its PEM bundle before starting the application; the engine address must still match the certificate. See the [URI model](002-uri-model.md) for explicit connection settings.

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
