# rstream-rtty-server

`rstream-rtty-server` exposes a WebTTY server backed by the C++ rtty implementation.

Use `--uri` to print an rstream tunnel URI for a published WebTTY endpoint. The generated URI includes the standard WebTTY discovery labels:

```text
application-protocol=rstream.webtty
rstream.webtty.capabilities=exec
rstream.webtty.exec.path=/
```
