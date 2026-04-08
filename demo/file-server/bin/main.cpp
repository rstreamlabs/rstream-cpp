// See LICENSE file in the project root for license information.

#include <iostream>
#include <thread>
#include <vector>

#include <boost/asio/io_context.hpp>
#include <boost/asio/signal_set.hpp>

#include <docopt.h>
#include <spdlog/sinks/basic_file_sink.h>

#include <rstream/config.hpp>
#include <rstream/core/exception.hpp>
#include <rstream/core/log.hpp>
#include <rstream/file-server/server.hpp>

static const char USAGE[] = R"(
rstream-file-server - https://rstream.io/ - file server using rstream primitives

this program is part of rstream-utils (https://rstream.io/download/utils) and was created using rstream C++ SDK (https://rstream.io/sdk)

usage:
  rstream-file-server [options]
  rstream-file-server (-h|--help)
  rstream-file-server --version

options:
  -h --help            show this screen
  --version            show version
  -v --verbose         enable verbose mode
  --uri=ARG            URI [default: 127.0.0.1:6004]
  -w --workdir=ARG     directory to serve files [default: .]
  -j --jobs=ARG        number of threads to run simultaneously (0 = auto) [default: 0]
)";

const auto version = std::string("rstream-file-server ") + RSTREAM_VERSION;

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
  boost::asio::io_context io_context(jobs);
  boost::asio::signal_set signal_set(io_context, SIGINT, SIGTERM);
  rstream::file_server::server::config config = {
      .m_address = args.at("--uri").asString(),
      .m_workdir = args.at("--workdir").asString(),
  };
  rstream::file_server::settings_server settings = {
      .m_timeouts_start_ms = 5000,
  };
  rstream::file_server::server server(io_context.get_executor(), config, settings);
  signal_set.async_wait([&server](const boost::system::error_code&, int) {
    server.cancel();
  });
  boost::system::error_code result;
  server.async_run([&signal_set, &result](const boost::system::error_code& error_code) {
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
