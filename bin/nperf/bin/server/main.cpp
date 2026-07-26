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
#include <rstream/nperf/nperf.hpp>
#include <rstream/nperf/server.hpp>

static const char USAGE[] = R"(
rstream-nperf-server - https://rstream.io/ - network performance server using rstream primitives

this program is distributed with the rstream C++ tools. See https://rstream.io/docs/integrations/cpp-sdk and https://github.com/rstreamlabs/rstream-cpp.

usage:
  rstream-nperf-server [options]
  rstream-nperf-server (-h|--help)
  rstream-nperf-server --version

options:
  -h --help             show this screen
  --version             show version
  -v --verbose          enable verbose mode
  --uri=ARG             URI [default: 127.0.0.1:6003]
  -j --jobs=ARG         number of threads to run simultaneously (0 = auto) [default: 0]
  --buffer-size=ARG     buffer size in bytes expressed as a power of 2 [default: 22]
  --protocol=ARG        protocol to use [default: websocket]

valid protocols: websocket, plain
)";

const auto version = std::string("rstream-nperf-server ") + RSTREAM_VERSION;

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
  std::cout << "\n\tnperf by rstream - https://rstream.io/\n"
            << std::endl;
  boost::asio::io_context io_context(jobs);
  boost::asio::signal_set signal_set(io_context, SIGINT, SIGTERM);
  rstream::nperf::protocol protocol;
  rstream::nperf::parse_protocol(protocol, args.at("--protocol").asString());
  rstream::nperf::server::config config = {
      .m_address = args.at("--uri").asString(),
  };
  rstream::nperf::settings_server settings = {
      .m_common = {
          .m_buffer_size            = static_cast<std::uint32_t>(std::max((long)0, (long)std::pow(2, args.at("--buffer-size").asLong()))),
          .m_timeouts_max_time_ms   = 60000,
          .m_timeouts_open_close_ms = 10000,
          .m_protocol               = protocol,
      },
      .m_timeouts_start_ms = 5000,
  };
  rstream::nperf::server server(io_context.get_executor(), config, settings);
  signal_set.async_wait([&server](const boost::system::error_code&, int) {
    server.cancel();
  });
  boost::system::error_code error;
  server.async_run([&signal_set, &error](const boost::system::error_code& error_code) {
    error = error_code;
    signal_set.cancel();
  });
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
  if (error) {
    std::cerr << error.message() << std::endl;
  }
  return error ? -1 : 0;
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
