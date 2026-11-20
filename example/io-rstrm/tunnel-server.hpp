// See LICENSE file in the project root for license information.

#pragma once

#include <csignal>
#include <exception>
#include <iostream>
#include <list>
#include <utility>

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/signal_set.hpp>

#include <rstream/core/exception.hpp>
#include <rstream/io-rstrm/client.hpp>
#include <rstream/io-rstrm/socket.hpp>

namespace example_tunnel {

class sessions {
 public:
  template <typename Serve>
  void start(rstream::io_rstrm::socket socket, Serve serve)
  {
    if (m_stopping) {
      boost::system::error_code ignored;
      socket.close(ignored);
      return;
    }
    // A typed socket copy retains the same stream for shutdown.
    auto active = m_sockets.insert(m_sockets.end(), socket);
    const auto executor = socket.get_executor();
    boost::asio::co_spawn(executor, serve(std::move(socket)), [this, active](std::exception_ptr error) {
      if (error && !m_stopping) {
        std::cerr << "session error: " << rstream::core::throwable::to_string(error) << std::endl;
      }
      m_sockets.erase(active);
    });
  }

  void stop()
  {
    m_stopping = true;
    for (auto& socket : m_sockets) {
      boost::system::error_code ignored;
      socket.close(ignored);
    }
  }

  bool stopping() const
  {
    return m_stopping;
  }

 private:
  std::list<rstream::io_rstrm::socket> m_sockets;
  bool m_stopping = false;
};

template <typename Listener>
int run(Listener listen)
{
  namespace asio = boost::asio;
  int result = 0;
  sessions active;
  asio::io_context io;
  rstream::io_rstrm::client client(io.get_executor());
  asio::signal_set signals(io, SIGINT, SIGTERM);
  signals.async_wait([&](const boost::system::error_code& error, int) {
    if (!error) {
      active.stop();
      client.close();
    }
  });
  asio::co_spawn(io, listen(client, active), [&](std::exception_ptr error) {
    if (error && !active.stopping()) {
      result = 1;
      std::cerr << "fatal error: " << rstream::core::throwable::to_string(error) << std::endl;
    }
    boost::system::error_code ignored;
    signals.cancel(ignored);
    active.stop();
    client.close();
  });
  // Keep the loop running while the SDK closes the control channel and drains its operations.
  io.run();
  return result;
}

}  // namespace example_tunnel
