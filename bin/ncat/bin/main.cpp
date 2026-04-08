// See LICENSE file in the project root for license information.

#include <cmath>
#include <iostream>
#include <thread>

#include <boost/asio/io_context.hpp>
#include <boost/asio/signal_set.hpp>

#include <docopt.h>

#include <rstream/config.hpp>
#include <rstream/core/exception.hpp>
#include <rstream/core/log.hpp>
#include <rstream/ncat/client.hpp>
#include <rstream/ncat/ncat.hpp>
#include <rstream/ncat/server.hpp>

static const char USAGE[] = R"(
rstream-ncat - https://rstream.io/ - netcat-like utility using rstream primitives

this program is part of rstream-utils (https://rstream.io/download/utils) and was created using rstream C++ SDK (https://rstream.io/sdk)

usage:
  rstream-ncat [options] <remote> [-i|-I] [--jobs=ARG]
  rstream-ncat [options] -L <local> (-e=ARG|-c=ARG) [-v] [--jobs=ARG]
  rstream-ncat [options] -L <local> -R <remote> [-v] [--jobs=ARG]
  rstream-ncat (-h|--help)
  rstream-ncat --version

example:
  rstream-ncat 127.0.0.1:1234
  rstream-ncat -L 127.0.0.1:1234 -c date
  rstream-ncat -L 127.0.0.1:1234 -R 127.0.0.1:8080

options:
  -h --help            show this screen
  --version            show version
  -v --verbose         enable verbose mode
  -i --interactive     enable interactive mode
  -I --no-interactive  disable interactive mode
  -e --exec=ARG        runs a command without shell interpretation
  -c --sh-exec=ARG     runs a command by passing it to a system shell
  -j --jobs=ARG        number of threads to run simultaneously (0 = auto) [default: 0]
  --buffer-size=ARG    buffer size in bytes expressed as a power of 2 [default: 22]
)";

const auto version = std::string("rstream-ncat ") + RSTREAM_VERSION;

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
  std::error_code result;
  enum class mode {
    none,
    client,
    server
  };
  enum mode mode = mode::none;
  {
    auto it = args.find("-L");
    if (it != args.end() && it->second.asBool()) {
      mode = mode::server;
    }
    else {
      mode = mode::client;
    }
  }
  std::shared_ptr<void> ptr;
  auto buffer_size = static_cast<std::uint32_t>(std::max((long)0, (long)std::pow(2, args.at("--buffer-size").asLong())));
  if (mode == mode::client) {
    rstream::ncat::client::config config = {
        .m_address         = args.at("<remote>").asString(),
        .m_interactive     = args.at("--interactive").asBool(),
        .m_non_interactive = args.at("--no-interactive").asBool(),
    };
    rstream::ncat::settings_client settings = {
        .m_common                        = {},
        .m_read_socket_buffer_size_bytes = buffer_size,
        .m_read_std_in_buffer_size_bytes = buffer_size,
    };
    auto client = std::make_shared<rstream::ncat::client>(io_context.get_executor(), config, settings);
    ptr         = client;
    signal_set.async_wait([client](const std::error_code&, int) { client->cancel(); });
    client->async_run([&signal_set, &result](const std::error_code& error_code) {
      result = error_code;
      signal_set.cancel();
    });
  }
  else if (mode == mode::server) {
    rstream::ncat::server::config config = {
        .m_local  = args.at("<local>").asString(),
        .m_remote = {},
    };
    rstream::ncat::settings_server settings = {
        .m_common                            = {},
        .m_read_downstream_buffer_size_bytes = buffer_size,
        .m_read_upstream_buffer_size_bytes   = buffer_size,
        .m_timeouts_ms                       = {
                                  .m_start = 5000,
                                  .m_open  = 10000,
        },
    };
    {
      auto it = args.find("-R");
      if (it != args.end() && it->second.asBool()) {
        rstream::io::address address = args.at("<remote>").asString();
        config.m_remote              = address;
      }
      else {
        it = args.find("--exec");
        if (it != args.end() && it->second.operator bool()) {
          config.m_remote = (rstream::ncat::server::exec){
              .m_shell = false,
              .m_cmd   = it->second.asString(),
          };
        }
        else {
          it = args.find("--sh-exec");
          if (it != args.end() && it->second.operator bool()) {
            config.m_remote = (rstream::ncat::server::exec){
                .m_shell = true,
                .m_cmd   = it->second.asString(),
            };
          }
        }
      }
    }
    auto server = std::make_shared<rstream::ncat::server>(io_context.get_executor(), config, settings);
    ptr         = server;
    signal_set.async_wait([server](const std::error_code&, int) { server->cancel(); });
    server->async_run([&signal_set, &result](const std::error_code& error_code) {
      result = error_code;
      signal_set.cancel();
    });
  }
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
