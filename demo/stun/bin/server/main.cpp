// See LICENSE file in the project root for license information.

#include <iostream>
#include <thread>
#include <vector>

#include <boost/asio/signal_set.hpp>

#include <docopt.h>

#include <rstream/config.hpp>
#include <rstream/core/exception.hpp>
#include <rstream/core/log.hpp>
#include <rstream/io/metrics.hpp>
#include <rstream/stun/server.hpp>

static const char USAGE[] = R"(
rstream-stun-server - https://rstream.io/ - STUN server

this program is distributed with the rstream C++ tools. See https://rstream.io/docs/integrations/cpp-sdk and https://github.com/rstreamlabs/rstream-cpp.

usage:
  rstream-stun-server [options] [--inet4-only|--inet6-only]
  rstream-stun-server (-h|--help)
  rstream-stun-server --version

options:
  -h --help            show this screen
  --version            show version
  -v --verbose         enable verbose mode
  --host=ARG           hostname [default: ::]
  -p --port=ARG        port number [default: 3478]
  -4 --inet4-only      force using IPv4
  -6 --inet6-only      force using IPv6
  -j --jobs=ARG        number of threads to run simultaneously (0 = auto) [default: 0]
  -m --metrics         expose prometheus metrics
  --metrics-addr=ARG   address to expose metrics on [default: 127.0.0.1:9090]
  -g --geoip           enable geoip metrics
  --geoip-db=ARG       geoip database [default: /var/lib/GeoIP/GeoLite2-Country.mmdb]
)";

const auto version = std::string("rstream-stun-server ") + RSTREAM_VERSION;

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
  auto jobs = std::max((long)0, args.at("--jobs").asLong());
  if (jobs == 0) {
    jobs = std::thread::hardware_concurrency();
  }
  bool metrics = false;
  {
    auto it = args.find("--metrics");
    if (it != args.end() && it->second.asBool()) {
      metrics = true;
    }
  }
  boost::asio::io_context io_context(jobs);
  boost::asio::signal_set signal_set(io_context, SIGINT, SIGTERM);
  rstream::stun::server::config config = {
#ifdef RSTREAM_WITH_GEOIP
      .m_geoip = {
          .m_enable            = args.at("--geoip").asBool(),
          .m_database_location = args.at("--geoip-db").asString(),
      },
#endif
      .m_host  = args.at("--host").asString(),
      .m_port  = args.at("--port").asString(),
      .m_inet4 = false,
      .m_inet6 = false,
  };
#ifndef RSTREAM_WITH_GEOIP
  if (args.at("--geoip").asBool()) {
    throw std::runtime_error("geoip support not available");
  }
#endif
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
  rstream::stun::settings_server settings = {
      .m_mtu = 4096,
  };
  rstream::stun::server server(io_context.get_executor(), config, settings);
  rstream::io::metrics::exposer::config exposer_config = {
      .m_address = args.at("--metrics-addr").asString(),
  };
  rstream::io::metrics::settings_exposer settings_exposer = {
      .m_timeouts_start_ms = 5000,
  };
  rstream::io::metrics::exposer exposer(io_context.get_executor(), exposer_config, settings_exposer);
  auto cancel = [&server, &exposer, &signal_set]() {
    server.cancel();
    exposer.cancel();
    signal_set.cancel();
  };
  boost::system::error_code result;
  auto completion_handler = [&result, cancel](const boost::system::error_code& error_code) {
    if (!result && error_code) {
      result = error_code;
    }
    cancel();
  };
  signal_set.async_wait(std::bind(cancel));
  server.async_run(completion_handler);
  if (metrics) {
    exposer.async_run(completion_handler);
  }
  std::vector<std::thread> threads;
  if (jobs > 1) {
    auto n = jobs - 1;
    threads.reserve(n);
    for (decltype(n) i = 0; i < n; ++i) {
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
