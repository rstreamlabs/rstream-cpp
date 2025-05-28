// See LICENSE file in the project root for license information.

#include <iostream>

#include <boost/asio/signal_set.hpp>

#include <docopt.h>

#include <rstream/config.hpp>
#include <rstream/core/exception.hpp>
#include <rstream/core/log.hpp>

#include "client.hpp"

static const char USAGE[] = R"(
rstream-stun-client - (https://rstream.io/) - STUN client

this program is part of rstream-utils (https://rstream.io/download/utils) and was created using rstream C++ SDK (https://rstream.io/sdk)

usage:
  rstream-stun-client [options] [--inet4-only|--inet6-only]
  rstream-stun-client (-h|--help)
  rstream-stun-client --version

options:
  -h --help             show this screen
  --version             show version
  -v --verbose          enable verbose mode
  --host=ARG            hostname [default: stun.rstream.io]
  -p --port=ARG         port number [default: 3478]
  --timeout=ARG         timeout (ms) [default: 2500]
  -b --bind=ARG         local address to bind
  -4 --inet4-only       force using IPv4
  -6 --inet6-only       force using IPv6
)";

const auto version = std::string("rstream-stun-client ") + RSTREAM_VERSION;

int run(int argc, char** argv)
{
  auto args    = docopt::docopt(USAGE, {argv + 1, argv + argc}, true, version);
  bool verbose = false;
  {
    auto it = args.find("--verbose");
    if (it != args.end() && it->second.asBool()) {
      verbose = true;
    }
  }
  if (verbose) {
    rstream::core::log::enable_ansicolor_stdout_mt();
  }
  rstream::core::default_logger()->info(version);
  boost::asio::io_context io_context;
  boost::asio::signal_set signal_set(io_context, SIGINT, SIGTERM);
  client::config config = {
      .m_host       = args.at("--host").asString(),
      .m_port       = args.at("--port").asString(),
      .m_timeout_ms = static_cast<unsigned int>(args.at("--timeout").asLong()),
      .m_address    = {},
      .m_inet4      = false,
      .m_inet6      = false,
  };
  if (args.at("--bind")) {
    config.m_address = boost::asio::ip::make_address(args.at("--bind").asString());
  }
  {
    auto it = args.find("--inet4-only");
    if (it != args.end() && it->second.asBool()) {
      config.m_inet4 = true;
    }
  }
  {
    auto it = args.find("--inet6-only");
    if (it != args.end() && it->second.asBool()) {
      config.m_inet6 = true;
    }
  }
  if (config.m_address && !config.m_inet4 && !config.m_inet6) {
    if (config.m_address.get().is_v4()) {
      config.m_inet4 = true;
    }
    else {
      config.m_inet6 = true;
    }
  }
  auto client = std::make_shared<class client>(io_context.get_executor(), config);
  signal_set.async_wait([client](const boost::system::error_code&, int) {
    client->cancel();
  });
  std::pair<boost::system::error_code, boost::asio::ip::address> result;
  client->async_run([&signal_set, &result](const boost::system::error_code& error_code, const boost::asio::ip::address& address) {
    result = std::make_pair(error_code, address);
    signal_set.cancel();
  });
  io_context.run();
  if (result.first) {
    std::cerr << "an error occurred: " << result.first.message() << std::endl;
  }
  else {
    std::cout << result.second << std::endl;
  }
  return 0;
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
