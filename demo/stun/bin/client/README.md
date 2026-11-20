# rstream-stun-client

Ask a STUN server for the address it sees for your UDP socket. To exercise
the client with no external services, first
[start the loopback STUN server](../server/README.md).

From a [configured native checkout](../../../../docs/001-building-from-source.md),
build the client:

```sh
cmake --build --preset conan-debug --target rstream-stun-client
```

On macOS or Linux, query the server from the repository root:

```sh
build/native/build/Debug/demo/stun/bin/client/rstream-stun-client \
  --host 127.0.0.1 --port 3478 --inet4-only
```

The client prints `127.0.0.1` and exits. Stop the server in its own terminal
with Ctrl+C.
