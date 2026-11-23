# Measure and diagnose native connections

Use nperf for a known server and client pair. For a long-running tunnel,
inspect its status or scrape its metrics to check whether the service
is healthy. The two checks answer different questions.

## Measure a local stream

Use the binaries from [rstream-utils](../012-command-line-tools.md#obtain-the-tools)
or build them from the configured source checkout.

```sh
cmake --build --preset conan-debug --target rstream-nperf-server rstream-nperf-client
```

In the macOS or Linux build directory, find the tools under
`bin/nperf/bin/server/` and `bin/nperf/bin/client/`. With MSVC, both are
under `bin/`.

Start the server on loopback and use one event-loop thread for this demo.

```sh
rstream-nperf-server --uri 127.0.0.1:6003 --jobs 1
```

In another terminal, perform a bounded run:

```sh
rstream-nperf-client ping --uri 127.0.0.1:6003 --max-ping 5 --progress 0 --format json --jobs 1
```

The command prints separate JSON records for the connection, handshake and
round-trip measurements. The final `ping` record has `sample.size` equal to 5. This is an application-level latency check over a
WebSocket connection. Use `--protocol plain` on both ends to measure
the plain nperf protocol. The client also supports `download` and
`upload`. Give a transfer a byte limit with `--max-data` and a
time limit with `--timeout` in milliseconds.

For a remote path, change the server and client addresses or relay the
server through a [private bytestream](001-streams.md#reach-a-private-service).
Keep the protocol and measurement settings fixed when comparing paths.
Restrict access to the test endpoint. Stop the server with Ctrl+C.

## Observe a running forwarder

With the [loopback service](000-forwarding.md#start-a-loopback-service)
already running, expose its forwarder's metrics on loopback:

```sh
rstream-tunnel 127.0.0.1:8080 --no-publish --name cpp-metrics-demo \
  --metrics --metrics-addr 127.0.0.1:9090
```

Then scrape that process:

```sh
curl -fsS http://127.0.0.1:9090/metrics
```

Expect Prometheus text. The metrics endpoint describes the forwarder.
Use the tunnel's status and a request through it to confirm end-to-end
availability.

## Inspect the native build

Build `rstream-inspect` in a Debug checkout. On macOS or Linux find
it under `bin/inspect/` in the build directory. With MSVC find it
under `bin/`.

```sh
rstream-inspect version -v -j
rstream-inspect plugins
```

The first command prints version and build information. The second lists
the loaded plugins and their registered elements.
[Plugin packaging](../009-plugins-and-packaging.md#verify-registration-and-loading)
explains how to troubleshoot a missing URI transport.

`rstream-gping` draws a live nperf latency graph in an interactive Unix
terminal when installed from a Python-enabled source build. Its launcher
and Python runtime are installed together. Start the loopback nperf server
again if it has stopped, then run the graph in a second terminal.

```sh
rstream-gping --uri 127.0.0.1:6003 --max-ping 10
```

Use the nperf CLI for automation and machine-readable output.
