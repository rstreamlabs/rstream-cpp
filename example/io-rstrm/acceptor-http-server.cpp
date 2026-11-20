// See LICENSE file in the project root for license information.

#include <csignal>
#include <exception>
#include <iostream>
#include <optional>
#include <string>

#include <boost/asio.hpp>

#include <rstream/core/exception.hpp>
#include <rstream/io-rstrm/acceptor.hpp>
#include <rstream/io-rstrm/io-rstrm.hpp>

#include "http-handler.hpp"

namespace asio  = boost::asio;
namespace rstrm = rstream::io_rstrm;

asio::awaitable<void> serve(rstrm::acceptor& acceptor, std::optional<rstrm::socket>& active, const bool& stopping)
{
  auto executor = co_await asio::this_coro::executor;
  while (!stopping) {
    auto& socket = active.emplace(executor);
    rstrm::endpoint peer;
    co_await acceptor.async_accept(socket, peer, asio::use_awaitable);
    co_await example_http::serve(socket);
  }
}

int run()
{
  asio::io_context io;
  rstrm::settings_acceptor settings;
  settings.m_tunnel_properties.m_publish      = true;
  settings.m_tunnel_properties.m_protocol     = rstrm::protocol::http;
  settings.m_tunnel_properties.m_http_version = std::string("http/1.1");
  rstrm::acceptor acceptor(io.get_executor(), settings);

  boost::system::error_code error;
  acceptor.bind(rstrm::make_endpoint(boost::none, boost::none).value(), error);
  if (error) {
    throw boost::system::system_error(error);
  }
  bool stopping = false;
  rstrm::acceptor::control_callbacks callbacks{};
  callbacks.m_on_status_cb = [&stopping](const rstrm::status_extd& status) {
    if (!stopping && status.m_status && status.m_status.value() != "online") {
      std::cerr << status.m_status.value() << std::endl;
    }
  };
  callbacks.m_on_tunnel_properties_cb = [](const rstrm::tunnel_properties& properties) {
    if (auto forwarding = rstrm::format_forwarding_address(properties)) {
      std::cout << "Forwarding address: " << forwarding.value() << std::endl;
    }
  };
  acceptor.set_control_callbacks(callbacks, error);
  if (error) {
    throw boost::system::system_error(error);
  }

  int result = 0;
  std::optional<rstrm::socket> active;
  asio::signal_set signals(io, SIGINT, SIGTERM);
  signals.async_wait([&](const boost::system::error_code& signal_error, int) {
    if (!signal_error) {
      stopping = true;
      boost::system::error_code ignored;
      acceptor.close(ignored);
      if (active) {
        active->close(ignored);
      }
    }
  });
  asio::co_spawn(io, serve(acceptor, active, stopping), [&](std::exception_ptr failure) {
    if (failure && !stopping) {
      result = 1;
      std::cerr << rstream::core::throwable::to_string(failure) << std::endl;
    }
    boost::system::error_code ignored;
    signals.cancel(ignored);
    acceptor.close(ignored);
  });
  io.run();
  return result;
}

int main()
{
  try {
    return run();
  }
  catch (const std::exception& error) {
    std::cerr << error.what() << std::endl;
    return 1;
  }
}
