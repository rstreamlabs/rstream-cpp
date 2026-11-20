# rstream-stun-server

Run a local UDP STUN endpoint to test the
[companion client](../client/README.md). This example binds to loopback.

From a [configured native checkout](../../../../docs/001-building-from-source.md),
build the server:

```sh
cmake --build --preset conan-debug --target rstream-stun-server
```

On macOS or Linux, start it from the repository root:

```sh
build/native/build/Debug/demo/stun/bin/server/rstream-stun-server \
  --host 127.0.0.1 --port 3478 --inet4-only --jobs 1
```

Leave it running for the client request. The client should report the
observed loopback address. Stop the server with Ctrl+C.
