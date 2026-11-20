# Serve a WebSocket with Beast

For a classic WebSocket upgrade, publish an
[HTTP tunnel with an HTTP/1.1 upstream](https://rstream.io/docs/tunnels/http-tunnels/connection-upgrades).
A caller connects to the Engine with `wss://`. Beast receives the
upgrade on the accepted rstream socket after edge TLS termination.

## Verify the echo service

[Configure the checkout and the example path](../../example/io-rstrm/README.md#prepare-the-example-binaries).
Build the WebSocket server from the repository root.

```sh
cmake --build --preset conan-debug --target rstream-example-websocket-server
```

Use a [context](../002-configuration.md) that allows published HTTP.
Start the [server from the checkout](../../example/io-rstrm/websocket-server.cpp).

```sh
rstream-example-websocket-server
```

In a browser developer console on a page that allows outbound WebSockets,
replace the example hostname with the printed forwarding hostname and port.
Use a browser that trusts the issuer of the edge certificate. This console
example assumes the browser can already access the endpoint. The browser
WebSocket API cannot set a bearer header. For a protected endpoint,
choose a caller flow from [HTTP authentication](https://rstream.io/docs/tunnels/http-tunnels/authentication)
and establish access before running the script.

```javascript
const ws = new WebSocket("wss://your-forwarding-host/");
ws.onopen = () => ws.send("hello");
ws.onmessage = ({ data }) => {
  console.log(data);
  ws.close(1000);
};
ws.onclose = ({ code, wasClean }) => console.log(code, wasClean);
```

Expect `hello`, followed by `1000 true` for the close. The echo endpoint expects a WebSocket
upgrade. Stop the server with Ctrl+C.

## Adapt an existing Beast session

Include `rstream/io-rstrm/beast.hpp` when wrapping
an `io_rstrm::socket` in `boost::beast::websocket::stream`. It
provides Beast's teardown and socket timeout hooks. Keep the WebSocket
session and buffers alive through completion. The sample handles the peer's
close frame and then explicitly closes the underlying socket.

The demo echoes text and binary messages up to 64 KiB. When adapting it,
apply the appropriate [HTTP caller policy](https://rstream.io/docs/tunnels/http-tunnels/authentication)
and enforce the service's own origin and authorization rules.
