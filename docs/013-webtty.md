# WebTTY with the C++ SDK

WebTTY carries terminal commands and file operations over rstream connections.
The native server owns process execution; the client owns terminal I/O and
server trust. This application protocol adds execution identities, endpoint
proofs, payload encryption and process cleanup to the underlying streams.

| Task                             | Guide                                                                                                        |
| -------------------------------- | ------------------------------------------------------------------------------------------------------------ |
| Configure a server               | [Native WebTTY server](../bin/webtty/bin/server/README.md): enrollment, execution account and authorization. |
| Connect a client                 | [Native WebTTY client](../bin/webtty/bin/client/README.md): endpoint, trust and identity.                    |
| Provision workspaces and devices | [Go reference CLI](https://rstream.io/docs/webtty/registered-servers).                                       |

The C++ client/server support plain and WebSocket transport. WebTransport is
available through the Go reference implementation. Direct `Open` sessions and
engine-managed attach/control capabilities have their own authorization paths.
Choose the server mode first, then configure the matching client identity and
trust. Local bearer authentication, registered engine authentication and
endpoint-authenticated E2E payload protection are distinct choices.

When modifying the native implementation, follow the cross-language vectors,
process ownership and security gates in the
[engineering guide](014-sdk-engineering.md#constrained-system-contract).
