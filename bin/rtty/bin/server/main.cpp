// See LICENSE file in the project root for license information.

#include <iostream>
#include <thread>
#include <vector>

#include <boost/asio/io_context.hpp>
#include <boost/asio/signal_set.hpp>

#include <docopt.h>

#include <rstream/config.hpp>
#include <rstream/core/exception.hpp>
#include <rstream/core/log.hpp>
#include <rstream/rtty/rtty.hpp>
#include <rstream/rtty/server.hpp>

static const char USAGE[] = R"(
rstream-rtty-server - (https://rstream.io/) - remote TTY server using rstream primitives

this program is part of rstream-utils (https://rstream.io/download/utils) and was created using rstream C++ SDK (https://rstream.io/sdk)

usage:
  rstream-rtty-server [options] [--uri|-w]
  rstream-rtty-server (-h|--help)
  rstream-rtty-server --version

options:
  -h --help             show this screen
  --version             show version
  -v --verbose          enable verbose mode
  --uri=ARG             URI [default: 127.0.0.1:6002]
  -w --web              publish the server on the web using rstream tunnels
  --protocol=ARG        protocol to use [default: websocket]
  -j --jobs=ARG         number of threads to run simultaneously (0 = auto) [default: 0]

valid protocols: websocket, plain
)";

const auto version = std::string("rstream-rtty-server ") + RSTREAM_VERSION;

int run(int argc, char** argv)
{
  auto args = docopt::docopt(USAGE, {argv + 1, argv + argc}, true, version);
  {
    auto it = args.find("--verbose");
    if (it != args.end() && it->second.asBool()) {
      rstream::core::log::enable_ansicolor_stdout_mt();
    }
  }
  rstream::core::default_logger()->info(version);
  auto jobs = std::max((long)0, args.at("--jobs").asLong());
  if (jobs == 0) {
    jobs = std::thread::hardware_concurrency();
  }
  boost::asio::io_context io_context(jobs);
  boost::asio::signal_set signal_set(io_context, SIGINT, SIGTERM);
  rstream::rtty::protocol::type protocol_type;
  rstream::rtty::protocol::parse_type(protocol_type, args.at("--protocol").asString());
  std::string uri;
  {
    auto it = args.find("--web");
    if (it != args.end() && it->second.asBool()) {
      if (protocol_type != rstream::rtty::protocol::type::websocket) {
        throw std::runtime_error("protocol must be set to websocket when using the web option");
      }
      uri = rstream::rtty::build_webtty_uri();
    }
    else {
      uri = args.at("--uri").asString();
    }
  }
  auto endpoint                        = rstream::rtty::parse_endpoint_config(uri, protocol_type);
  rstream::rtty::server::config config = {
      .m_address       = endpoint.m_address,
      .m_protocol_type = protocol_type,
  };
  rstream::rtty::settings_server settings = {
      .m_common = {
          .m_mtu         = 1024 * 1024,
          .m_timeouts_ms = {
              .m_open      = 5000,
              .m_close     = 5000,
              .m_heartbeat = 5000,
          },
      },
      .m_timeouts_start_ms   = 5000,
      .m_std_out_buffer_size = 800 * 1024,
      .m_std_err_buffer_size = 800 * 1024,
  };
  rstream::rtty::server server(io_context.get_executor(), config, settings);
  signal_set.async_wait([&server](const std::error_code&, int) { server.cancel(); });
  std::error_code result;
  server.async_run([&signal_set, &result](const std::error_code& error_code) {
    result = error_code;
    signal_set.cancel();
  });
  std::vector<std::thread> threads;
  if (jobs > 1) {
    auto n = jobs - 1;
    threads.reserve(n);
    for (unsigned int i = 0; i < n; ++i) {
      threads.emplace_back(std::bind((boost::asio::io_context::count_type(boost::asio::io_context::*)()) & boost::asio::io_context::run, &io_context));
    }
  }
  io_context.run();
  for (auto& thread : threads) {
    thread.join();
  }
  threads.clear();
  if (result) {
    std::cerr << result.message() << std::endl;
  }
  return result ? -1 : 0;
}

int main(int argc, char** argv)
{
  std::exception_ptr error;
  try {
    return run(argc, argv);
  }
  catch (...) {
    error = std::current_exception();
  }
  if (error) {
    std::cerr << "a fatal error occurred: " << rstream::core::throwable::message(error) << std::endl;
  }
  return error ? -1 : 0;
}
