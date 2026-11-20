# Forward a service with rstream-tunnel

Use `rstream-tunnel` when an application already listens locally. The
tool creates a tunnel and relays each accepted stream to the local target.
Install the [native tools](../012-command-line-tools.md#obtain-the-tools)
and configure a [project or direct Engine context](../002-configuration.md).
The publishing identity must be allowed to create the requested tunnel.

## Start a loopback service

Run an HTTP server containing one file. Use Python 3 and an available
local port. In the first terminal on macOS or Linux:

```sh
demo_dir=$(mktemp -d) &&
  printf 'hello from rstream\n' > "$demo_dir/index.html" &&
  python3 -m http.server 8080 --bind 127.0.0.1 --directory "$demo_dir"
```

In Windows PowerShell with the Python launcher:

```powershell
$demoDir = Join-Path ([System.IO.Path]::GetTempPath()) ([Guid]::NewGuid().ToString())
New-Item -ItemType Directory -Path $demoDir | Out-Null
Set-Content -Encoding ascii -Path (Join-Path $demoDir 'index.html') -Value 'hello from rstream'
py -3 -m http.server 8080 --bind 127.0.0.1 --directory $demoDir
```

In another terminal, request the file. Use `curl.exe` on Windows.

```sh
curl -fsS http://127.0.0.1:8080/
```

Expect `hello from rstream`. Keep the loopback server running for the
examples below.

## Publish the HTTP service

Open an HTTP tunnel on a project that permits publication. A public
caller can reach the demo according to the project's access policy.
Use the protected example below when caller authentication is required.

```sh
rstream-tunnel 127.0.0.1:8080 --http --publish --format human
```

Wait for the `status` field to read `online`. Copy the HTTPS address in `forwarding`.
The `forwarded` field is the local target. Open the public address
with your browser or run the following with the printed URL:

```sh
curl -fsS 'https://your-forwarding-host/'
```

Expect the same response. With a private CA, use `curl --cacert` and
keep the published hostname in the URL. Stop `rstream-tunnel` with
Ctrl+C when done. The loopback server stays running for the
[private example](001-streams.md#reach-a-private-service).

### Require a caller token

On a deployment that supports HTTP caller authentication, publish with
`--token-auth`. The agent context still authenticates the tunnel owner.

```sh
rstream-tunnel 127.0.0.1:8080 --http --publish --token-auth --format human
```

Take the new tunnel's `forwarding` URL. Make its
[scoped caller token](https://rstream.io/docs/tunnels/http-tunnels/authentication#token-authentication)
available as `TUNNEL_ACCESS_TOKEN` from your secret store. In a POSIX
shell, replace the example URL with the one just printed.

```sh
forwarding_url='https://your-forwarding-host/'
curl -sS -o /dev/null -w '%{http_code}\n' "$forwarding_url"
curl -fsS -H "Authorization: Bearer $TUNNEL_ACCESS_TOKEN" "$forwarding_url"
```

For a tunnel protected by token alone, expect 401 from the first request
and `hello from rstream` from the second.

Other project policies can restrict source networks, geography, or access
by an authenticated browser. [HTTP authentication](https://rstream.io/docs/tunnels/http-tunnels/authentication)
describes the caller experience and how to combine the available controls.

## Choose TCP or TLS for a different service

| Mode | Command option | What the application provides |
| --- | --- | --- |
| Raw TCP | `--tcp` | A service that owns its byte protocol and authentication, such as SSH. |
| Edge-terminated TLS | `--tls --tls-mode terminated` | A service receiving plaintext by default. `--upstream-tls` asks the Engine to open a separate TLS connection to a TLS-speaking upstream. |
| TLS passthrough | `--tls --tls-mode passthrough` | A TLS service presenting a certificate for the forwarding hostname. The caller verifies the application certificate. |

For an existing SSH server listening on loopback, create a TCP publication:

```sh
rstream-tunnel 127.0.0.1:22 --tcp --format human
```

Use the hostname and port before the `(tcp)` suffix in
`forwarding`. For example, if it reports `ssh.example.com:12345`,
connect with `ssh -p 12345 your-user@ssh.example.com` after substituting
the printed values and your account. Confirm receipt of the service's
host key and authenticate with your usual SSH credential. Use
`--tcp-port` only for a port already reserved by your project. See
[public TCP availability](https://rstream.io/docs/tunnels/tcp-tunnels)
for supported deployments and CE's ephemeral-port behavior.

### Pass TLS through to the service

Choose an allowed forwarding hostname and a certificate covering it.
In the demo terminal, serve the same file over TLS with Python 3.14 or
later. Set `SERVICE_CERT` and `SERVICE_KEY` to your certificate chain
and private key files.

```sh
python3 -m http.server 8443 --bind 127.0.0.1 --directory "$demo_dir" \
  --tls-cert "$SERVICE_CERT" --tls-key "$SERVICE_KEY"
```

In the forwarder's terminal, set `FORWARDING_HOST` to the name on the
certificate and request that hostname.

```sh
rstream-tunnel 127.0.0.1:8443 --tls --tls-mode passthrough \
  --publish --host "$FORWARDING_HOST"
```

When the tunnel is online, request the printed HTTPS URL. Set
`SERVICE_CA` to the issuer for a private certificate, or use the HTTP
client's normal trust store for a public certificate.

```sh
curl -fsS --cacert "$SERVICE_CA" 'https://your-forwarding-host/'
```

The same demo file is returned after the caller validates the application's
certificate. For a C++ service that creates its own tunnel, see
[serve TLS with Beast](../beast/000-tls.md).

When terminating TLS at the Engine, use `--tls-min-version` or
`--mtls` to apply the appropriate edge policy. The agent's own client
certificate and the public caller's certificate are independent. See
[published mTLS access](https://rstream.io/docs/security/mtls#published-tunnel-access)
for admission requirements. Keep passthrough TLS settings in the application.

## Diagnose the forwarder

| Observation | Check |
| --- | --- |
| No online status | Select the intended context and verify the Engine certificate, token and publication rights. |
| Online, request fails at the edge | Check the caller credential, DNS and certificate for the forwarding hostname. |
| Online, local target fails | Request the local address directly. Match the upstream protocol and port. |
| Connection repeatedly drops | Watch the status events or loopback [metrics](002-measurement.md#observe-a-running-forwarder). |

With `--format json`, the tool writes line-oriented events. In an
existing pipeline, select online tunnel records with
`jq -r 'select(.type == "tunnel_status" and .status == "online") | .forwarding // empty'`.
A TLS, TCP or private display value can include a suffix after the address.
`--no-retry` disables control reconnection. Tunnel creation has its own
retry behavior. Use an external deadline if a check must fail within a
fixed time.
