# rstream-file-server

Serve files from a selected directory over HTTP. This example keeps the
listener on loopback and serves only a disposable directory.

From a [configured native checkout](../../../docs/001-building-from-source.md),
build the demo:

```sh
cmake --build --preset conan-debug --target rstream-file-server
```

On macOS or Linux, start it from the repository root:

```sh
demo_dir=$(mktemp -d)
printf 'hello from the file server\n' > "$demo_dir/example.txt"
build/native/build/Debug/demo/file-server/bin/rstream-file-server \
  --uri 127.0.0.1:6004 --workdir "$demo_dir" --jobs 1
```

In another terminal, open `http://127.0.0.1:6004/` for the directory listing
or download the file:

```sh
curl -fsS http://127.0.0.1:6004/api/file/example.txt
```

Expect `hello from the file server`. Stop the server with Ctrl+C and
remove the temporary directory from the first terminal with
`rm -r -- "$demo_dir"`.

| GET path | Result |
| --- | --- |
| `/` | Redirect to `/www`. |
| `/www/` or `/www/<directory>` | HTML listing for a directory. |
| `/api/file/<path>` | Download a file relative to the working directory. |
