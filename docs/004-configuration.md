# C++ SDK configuration

Before using SDK or CLI flows, login and project/context setup are required through the `rstream` CLI. The standard developer-machine path is browser-based login:

```bash
rstream login
```

Then select the target project endpoint and set the default context:

```bash
rstream project use <project-endpoint> --default
```

`rstream-cpp` consumes the same configuration file and context model as the `rstream` CLI (default path: `~/.rstream/config.yaml`).

For advanced authentication modes and context workflows, see the rstream CLI workflow documentation:

- https://github.com/rstreamlabs/rstream-go/blob/main/docs/001-cli-workflow.md

## Environment variables

The SDK and CLI flows use the same configuration concepts, and the variables below participate in explicit resolution paths inside `io-rstrm`.

For consistency with CLI flows, the following variables are the primary explicit overrides:

- `RSTREAM_CONFIG`: Override the config file path. If unset, the SDK falls back to `~/.rstream/config.yaml`.
- `RSTREAM_CONTEXT`: Select the active context during config-based resolution.
- `RSTREAM_API_URL`: Override the Control plane API URL used by context-aware resolution paths.
- `RSTREAM_ENGINE_ADDRESS`: Highest-priority engine address override for runtime connection targets.
- `RSTREAM_ENGINE`: Secondary engine override; when provided as `host:port`, it is expanded to a default `tcp://...` URI with TLS and ALPN defaults.
- `RSTREAM_AUTHENTICATION_TOKEN`: Override token resolution when token behavior is not fixed in SDK options.
- `RSTREAM_MTLS_CERT_FILE`: Client certificate file for mTLS agent authentication.
- `RSTREAM_MTLS_KEY_FILE`: Client private key file for mTLS agent authentication.
- `RSTREAM_TUNNEL_TRANSPORT`: Select `auto`, `tls`, or `quic` for the engine connection.
- `RSTREAM_QUIC_TRANSPORT`: Legacy transport selector. Prefer `RSTREAM_TUNNEL_TRANSPORT`.

### Resolution behavior

In short, engine resolution starts from explicit environment overrides and then falls back to config/context resolution. Token resolution starts from explicit SDK options, then environment override, then config-derived values. Token authentication and mTLS agent authentication are mutually exclusive for the control-channel connection. When the mTLS certificate and key variables are set, config-derived tokens are not used for that connection; setting mTLS variables together with `RSTREAM_AUTHENTICATION_TOKEN` is an error. Engine HTTP API requests use token authentication.

The shared YAML schema includes `transport.proxy` for SDKs that can proxy their engine connection. `rstream-cpp` parses that block, including the proxy TLS fields `caFile`, `serverName`, and `insecureSkipVerify`, so configuration files keep the same shape across SDKs. It does not currently implement HTTP CONNECT, SOCKS5, or environment-derived proxying for the `io-rstrm` engine connection. A selected context or environment that requests a proxy fails during configuration resolution instead of being ignored.

The same schema accepts `transport.mode: auto|tls|quic`. The C++ runtime
currently implements TLS only, so the default `auto` mode resolves to TLS.
Explicit `quic`, invalid values, and legacy `transport.useQuic: true` fail
during resolution instead of being silently ignored.

The external mTLS signer backend (`auth.mtls.storage.kind: exec`) is supported
by the Go SDK and CLI. Selecting it in C++ returns `unsupported_mtls_exec` with
an explicit unsupported-mode message before connecting; C++ never launches the
helper. Other contexts in the same file remain usable.

Contexts can authenticate an agent control-channel connection with mTLS instead of a token:

```yaml
contexts:
  - name: prod-mtls
    engine: project.cluster.example:443
    auth:
      mtls:
        certificateFile: /etc/rstream/client.pem
        keyFile: /etc/rstream/client-key.pem
```

Inline `certificate` and `key` values are also supported for generated or ephemeral configs.
