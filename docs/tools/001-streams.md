# Connect and relay streams with rstream-ncat

`rstream-ncat` reads and writes an asynchronous stream. Give it a
remote address to connect standard input and output, or combine a local
listener and remote address to relay streams with no change in the
application protocol.

## Verify a local relay

Start the [loopback HTTP service](000-forwarding.md#start-a-loopback-service).
In a second terminal, relay requests from local port 18080 to port 8080.

```sh
rstream-ncat -L 127.0.0.1:18080 -R 127.0.0.1:8080
```

From a third terminal, check that the relay preserves the HTTP response.
Use `curl.exe` on Windows.

```sh
curl -fsS http://127.0.0.1:18080/
```

Expect `hello from rstream`. Keep both listeners on loopback for this
test. Stop the relay with Ctrl+C before reusing the port.

You can also connect standard input and output to the origin. On a POSIX
shell, send a request with an explicit end to the header block:

```sh
printf 'GET / HTTP/1.0\r\nHost: localhost\r\n\r\n' | rstream-ncat 127.0.0.1:8080
```

The output contains HTTP 200 and the same marker. Plain addresses select
TCP. `-i` and `-I` set interactive mode explicitly when needed.

## Reach a private service

Run the owner and caller with authenticated [contexts](../002-configuration.md)
for the same project or direct Engine. The owner needs permission to
create the private tunnel. The caller needs permission to dial it.
Choose a name that is available in that project.

With the demo server still running, start the owner:

```sh
rstream-tunnel 127.0.0.1:8080 --no-publish --name cpp-http-demo --format human
```

Wait for it to report `online`. It prints `rstrm://cpp-http-demo`
as a private endpoint. In the caller's terminal, expose a loopback socket
that dials the private tunnel:

```sh
rstream-ncat -L 127.0.0.1:18080 -R rstrm://cpp-http-demo
```

Run the same curl request to `http://127.0.0.1:18080/`. The service
returns `hello from rstream`. Stop the caller relay, tunnel and demo
server when done.

Use the caller's context to control which private tunnel the relay can
reach. Any process that can access its local listening socket can use
that relay, so keep it on the intended interface. The tunnel carries the
upstream application's HTTP bytes. To preserve a service's own TLS,
relay its TLS bytes instead and verify its certificate at the HTTP client.

## Select a transport

| Address | Use |
| --- | --- |
| `127.0.0.1:8080` or `tcp://127.0.0.1:8080` | An ordinary local TCP stream. |
| `rstrm://cpp-http-demo` | A private tunnel by name or ID. |
| `tcp://service.example:443?ssl` | TLS on a TCP stream. Review the [TLS stream options](../011-uri-reference.md#tls-stream-options) for trust and certificate identity. |

The [URI reference](../011-uri-reference.md) describes plugin selection and
overrides. To send UDP datagrams, use the
[Go CLI datagram mode](https://rstream.io/docs/tunnels/private-tunnels#netcat-datagram-mode).
