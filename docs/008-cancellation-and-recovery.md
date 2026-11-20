# Cancellation and recovery

Associate an Asio cancellation slot when an operation should be cancelled
independently. Retain the signal and the operation's referenced state until
completion. That completion is the point at which a borrowed buffer can be
released.

## Bound an exchange

The [private echo](../example/io-rstrm/private-echo.cpp) runs accept and dial
together. Either side's failure cancels its peer. A timer bounds the full
exchange, and `experimental::parallel_group` waits for both completions before
releasing their state. The program propagates the error and closes the control
client on success and failure.

Use a timeout for the operation the application wants to bound: setup, one
request or the whole session. A session deadline and an idle timeout answer
different questions; both belong to the service's lifecycle.

## Close the owner of the work

| Action                    | Effect                                                                                        |
| ------------------------- | --------------------------------------------------------------------------------------------- |
| Cancel an operation       | Requests completion with cancellation; its buffers remain borrowed until the completion runs. |
| Close a socket | Ends that stream and its pending operations. Copies of a typed socket refer to the same stream. |
| Close a tunnel or acceptor | Stops its accept path and releases its publication. The application still owns the sockets it has handed to sessions. |
| Close a control client | Ends its control connection and owned tunnel work. |
| Stop the application loop | Stops event processing for the application as a whole.                                        |

Stop accepting new work, choose whether to close or drain active sessions,
and keep polling the executor until their completions run. Use a separate
deadline for draining if requests can outlive shutdown. The
[typed HTTP example](003-tunnel-patterns.md#use-a-typed-rstream-acceptor) closes its
pending accept and active socket on shutdown.

## Reconnect deliberately

With the direct client, reconnect and recreate tunnels in response to its
status and disconnect callbacks. Treat the new tunnel handles and forwarding
addresses as a new generation. With a typed acceptor,
`settings_acceptor` controls reconnection and tunnel recreation independently.
The next published address is available in the status callback. Neither
interface resumes application exchanges that lost their underlying streams.
