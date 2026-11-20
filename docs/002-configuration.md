# C++ SDK configuration

The SDK reads the rstream CLI’s configuration and selected context, by default from `~/.rstream/config.yaml`. Applications and devices can also receive an explicit engine address and credentials without an interactive login.

## Connect to a hosted project

Use an [installed CLI](https://rstream.io/docs/getting-started/installation).
Sign in and copy the `ENDPOINT` from the project list.

```sh
rstream login
rstream project list
rstream project use 'my-project-endpoint'
```

Project selection saves the default context. If the list is empty, follow
[hosted project setup](https://rstream.io/docs/getting-started/quickstart-guide#select-a-project)
to create a project or ask its owner to grant access. The SDK uses that context
when the application constructs its client. Choose an identity with the
rights to create a tunnel or dial a private stream, as appropriate.

## Connect to a direct Engine

With self-hosted Community Edition, use the Engine hostname and port, a
locally signed JWT and the CA for that Engine when it uses private trust.
Follow [CE agent context setup](https://rstream.io/docs/tunnels/self-hosted/deployment#agent-context)
to create the CLI context. Use the same context when running a C++ application.
If you set `RSTREAM_CONFIG` to isolate the configuration, keep it set in the
process launching the application. The certificate on the published HTTP
address must also be verifiable by the caller.

## Environment variables

| Variable                       | Purpose                                                                                                    |
| ------------------------------ | ---------------------------------------------------------------------------------------------------------- |
| `RSTREAM_CONFIG`               | Configuration file path; defaults to `~/.rstream/config.yaml`.                                             |
| `RSTREAM_CONTEXT`              | Context to select from the file.                                                                           |
| `RSTREAM_API_URL`              | Select the matching environment in the local configuration by its API URL.                                                              |
| `RSTREAM_ENGINE_ADDRESS`       | Highest-priority engine address override.                                                                  |
| `RSTREAM_ENGINE`               | Engine override when `RSTREAM_ENGINE_ADDRESS` is absent; `host:port` selects the default TLS URI and ALPN. |
| `RSTREAM_AUTHENTICATION_TOKEN` | Token override when token behavior is not fixed in SDK options.                                            |
| `RSTREAM_MTLS_CERT_FILE`       | Client certificate file for mTLS authentication.                                                           |
| `RSTREAM_MTLS_KEY_FILE`        | Client private key file for mTLS authentication.                                                           |
| `RSTREAM_TUNNEL_TRANSPORT` | Engine transport selector. `auto` and `tls` select TLS. |
| `RSTREAM_QUIC_TRANSPORT`       | Legacy transport selector; prefer `RSTREAM_TUNNEL_TRANSPORT`.                                              |

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
needed, `ssl.sni`, as shown in the [URI reference](011-uri-reference.md#tls-stream-options).
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

## PKCS#11 identities

PKCS#11 mTLS keeps the private key in a token or HSM while the SDK requests TLS
handshake signatures. Use the OpenSSL variant of the SDK with
`RSTREAM_WITH_PKCS11` enabled. A source build selects
`-DSSL_PROVIDER=openssl -DRSTREAM_WITH_PKCS11=ON`; a Conan consumer selects
`rstream/*:ssl_provider=openssl` and needs a package built with that support.
LibreSSL builds and packages without PKCS#11 support reject this storage mode.
The application still links the normal SDK/runtime plugin targets.

Install an OpenSSL PKCS#11 provider discoverable by the OpenSSL library your
application uses. Its name (such as `pkcs11prov`) selects the provider.
The `module` field instead names the vendor's PKCS#11 library.

Use [credential storage](https://rstream.io/docs/security/credential-storage#pkcs11-mtls-storage)
for the shared YAML fields. Select one token, one key and one certificate in
`auth.mtls.storage`. Keep the PIN in the configured environment variable
and leave file or inline mTLS aliases unset. The C++ runtime rejects the
Go-only `slot` and `maxSessions` selectors, macOS Keychain storage and
external `exec` signers. The [SoftHSM walkthrough](https://rstream.io/guides/secure-rstream-agent-authentication-with-keychain-and-pkcs11#test-pkcs11-locally-with-softhsm)
covers provider setup, enrollment and verification.

## Transport boundaries

| Configuration                                                                     | C++ behavior                                                                                                                            |
| --------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------- |
| `transport.mode: auto` or `tls`                                                   | Uses TLS.                                                                                                                               |
| `transport.mode: quic`, an invalid mode, or legacy `transport.useQuic: true`      | Fails during resolution.                                                                                                                |
| `transport.proxy`, including HTTP CONNECT, SOCKS5 or environment-derived proxying | The shared schema is parsed, but a selected configuration requesting a proxy fails because the engine connection does not implement it. |
| `auth.mtls.storage.kind: exec`                                                    | Returns `unsupported_mtls_exec` before connecting and does not launch the helper. Use the Go SDK or CLI for this storage backend.       |

Unsupported settings in a selected context do not prevent using other supported contexts in the same configuration file. Protocol capabilities are summarized in the [SDK overview](../README.md).
