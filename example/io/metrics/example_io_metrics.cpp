// See LICENSE file in the project root for license information.

#include <atomic>
#include <chrono>
#include <iostream>
#include <sstream>
#include <thread>

#include <boost/asio/io_context.hpp>
#include <boost/asio/signal_set.hpp>

#include <docopt.h>

#include <rstream/config.hpp>
#include <rstream/core/exception.hpp>
#include <rstream/core/log.hpp>
#include <rstream/core/metrics.hpp>
#include <rstream/io/metrics.hpp>

static const char USAGE[] = R"(
rstream-example-io-metrics

usage:
  rstream-example-io-metrics [options]
  rstream-example-io-metrics (-h|--help)
  rstream-example-io-metrics --version

options:
  -h --help            show this screen
  --version            show version
  -v --verbose         enable verbose mode
  -j --jobs=ARG        number of threads to run simultaneously (0 = auto) [default: 0]
  --metrics-addr=ARG   address to expose metrics on [default: 127.0.0.1:9090]
)";

const auto version = std::string("rstream-example-io-metrics ") + RSTREAM_VERSION;

std::atomic<bool> m_running;

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
  auto jobs = std::max((long)0, args.at("--jobs").asLong());
  if (jobs == 0) {
    jobs = std::thread::hardware_concurrency();
  }
  boost::asio::io_context io_context(jobs);
  boost::asio::signal_set signal_set(io_context, SIGINT, SIGTERM);
  rstream::io::metrics::exposer::config config = {
      .m_address = args.at("--metrics-addr").asString(),
  };
  rstream::io::metrics::settings_exposer settings = {
      .m_timeouts_start_ms = 5000,
  };
  rstream::io::metrics::exposer exposer(io_context.get_executor(), config, settings);
  signal_set.async_wait([&exposer](const boost::system::error_code&, int) { exposer.cancel(); });
  boost::system::error_code result;
  exposer.async_run([&signal_set, &result](const boost::system::error_code& error_code) {
    result = error_code;
    signal_set.cancel();
  });
  std::vector<std::thread> threads;
  threads.reserve(jobs);
  if (jobs > 1) {
    auto n = jobs - 1;
    for (unsigned int i = 0; i < n; ++i) {
      threads.emplace_back(std::bind((boost::asio::io_context::count_type(boost::asio::io_context::*)()) & boost::asio::io_context::run, &io_context));
    }
  }
  m_running = true;
  auto loop = [&exposer]() {
    auto registry = std::make_shared<rstream::core::metrics::registry>();
    exposer.add_collectable(registry);
    auto counter = rstream::core::metrics::builder<rstream::core::metrics::counter>()
                       .name("my_requests_total")
                       .help("HTTP Failures")
                       .labels({{"method", "endpoint"}})
                       .registry(registry)
                       .build();
    while (m_running) {
      counter.increment();
      std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    }
  };
  threads.emplace_back(loop);
  io_context.run();
  m_running = false;
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
