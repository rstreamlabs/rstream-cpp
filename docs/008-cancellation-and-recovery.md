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
| Close a socket            | Ends that shared connection and its outstanding work.                                         |
| Close a tunnel            | Stops the tunnel's publication and accept path.                                               |
| Close a control client    | Ends that client's control connection and owned tunnel work.                                  |
| Stop the application loop | Stops event processing for the application as a whole.                                        |

Let pending completions drain when closing one service inside a larger process.
The long-running example servers use interrupts for process shutdown; a service
embedding them should define how existing sessions drain.

## Reconnect deliberately

The direct client exposes status/disconnect callbacks. The application decides
when to reconnect and recreate its tunnels. The acceptor offers automatic
reconnect and tunnel recreation through `settings_acceptor`, with bounded retry
intervals. A supervisor should stop retries when the application shuts down and
recreate a complete service generation after connection loss.
