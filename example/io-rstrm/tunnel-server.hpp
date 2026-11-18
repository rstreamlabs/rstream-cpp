// See LICENSE file in the project root for license information.

#pragma once

#include <csignal>
#include <exception>
#include <iostream>

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/signal_set.hpp>

#include <rstream/core/exception.hpp>
#include <rstream/io-rstrm/client.hpp>

namespace example_tunnel {

template <typename Listener>
int run(Listener listen)
{
  namespace asio = boost::asio;
  int result = 0;
  bool stopping = false;
  asio::io_context io;
  rstream::io_rstrm::client client(io.get_executor());
  asio::signal_set signals(io, SIGINT, SIGTERM);
  signals.async_wait([&](const boost::system::error_code& error, int) {
    if (!error) {
      stopping = true;
      client.close();
    }
  });
  asio::co_spawn(io, listen(client), [&](std::exception_ptr error) {
    if (error && !stopping) {
      result = 1;
      std::cerr << "fatal error: " << rstream::core::throwable::to_string(error) << std::endl;
    }
    boost::system::error_code ignored;
    signals.cancel(ignored);
    client.close();
  });
  // Keep the loop running while the SDK closes the control channel and drains its operations.
  io.run();
  return result;
}

}  // namespace example_tunnel
