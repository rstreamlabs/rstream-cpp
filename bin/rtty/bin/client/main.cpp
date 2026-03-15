// See LICENSE file in the project root for license information.

#include <algorithm>
#include <iostream>
#include <thread>
#include <utility>
#include <vector>

#include <boost/asio/io_context.hpp>
#include <boost/asio/signal_set.hpp>

#include <docopt.h>

#include <rstream/config.hpp>
#include <rstream/core/exception.hpp>
#include <rstream/core/log.hpp>
#include <rstream/rtty/client.hpp>
#include <rstream/rtty/rtty.hpp>

static const char USAGE[] = R"(
rstream-rtty-client - (https://rstream.io/) - remote TTY client using rstream primitives

this program is part of rstream-utils (https://rstream.io/download/utils) and was created using rstream C++ SDK (https://rstream.io/sdk)

usage:
  rstream-rtty-client [options] [-e=ARG...] [-i|-I] [-t|-T] [--] [<cmd>...]
  rstream-rtty-client (-h|--help)
  rstream-rtty-client --version

options:
  -h --help             show this screen
  --version             show version
  -v --verbose          enable verbose mode
  --uri=ARG             URI [default: 127.0.0.1:6002]
  -i --interactive      enable interactive mode
  -I --no-interactive   disable interactive mode
  -H --no-heartbeat     disable heartbeat mechanism
  -t --tty              enable TTY allocation
  -T --no-tty           disable TTY allocation
  -e --env=ARG          pass environment variable
  -w --workdir=ARG      set the working directory
  -u --user=ARG         username or UID
  --protocol=ARG        protocol to use [default: websocket]
  -j --jobs=ARG         number of threads to run simultaneously (0 = auto) [default: 0]

valid protocols: websocket, plain
)";

const auto version = std::string("rstream-rtty-client ") + RSTREAM_VERSION;

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
  rstream::rtty::protocol::type protocol_type;
  rstream::rtty::protocol::parse_type(protocol_type, args.at("--protocol").asString());
  rstream::rtty::client::config config = {
      .m_address          = rstream::io::address(args.at("--uri").asString()),
      .m_websocket_target = protocol_type == rstream::rtty::protocol::type::websocket ? boost::optional<std::string>("/") : boost::none,
      .m_protocol_config  = {
           .m_protocol_type = protocol_type,
           .m_options       = {},
           .m_env_vars      = {},
           .m_cmd_args      = {},
           .m_workdir       = {},
           .m_username      = {},
      },
  };
  {
    const auto& env_vars = args.at("--env").asStringList();
    rstream::rtty::protocol::parse_environment(config.m_protocol_config.m_env_vars, env_vars);
  }
  {
    const auto& cmd = args.at("<cmd>").asStringList();
    for (const auto& arg : cmd) {
      config.m_protocol_config.m_cmd_args.push_back(arg);
    }
  }
  {
    auto interactive     = args.at("--interactive").asBool();
    auto non_interactive = args.at("--no-interactive").asBool();

    if (!interactive && !non_interactive) {
      if (config.m_protocol_config.m_cmd_args.empty()) {
        interactive = true;
      }
    }
    config.m_protocol_config.m_options.m_interactive = interactive;
  }
  {
    auto tty     = args.at("--tty").asBool();
    auto non_tty = args.at("--no-tty").asBool();
    if (!tty && !non_tty) {
      if (config.m_protocol_config.m_cmd_args.empty()) {
        tty = true;
      }
    }
    config.m_protocol_config.m_options.m_allocate_tty = tty;
  }
  config.m_protocol_config.m_options.m_send_heartbeat = !args.at("--no-heartbeat").asBool();
  {
    auto workdir = args.at("--workdir");
    if (workdir) {
      config.m_protocol_config.m_workdir = workdir.asString();
    }
  }
  {
    auto username = args.at("--user");
    if (username) {
      rstream::rtty::protocol::parse_username(config.m_protocol_config.m_username, username.asString());
    }
  }
  rstream::rtty::settings_client settings = {
      .m_common = {
          .m_mtu         = 1024 * 1024,
          .m_timeouts_ms = {
              .m_open      = 5000,
              .m_close     = 5000,
              .m_heartbeat = 5000,
          },
      },
      .m_std_in_buffer_size = 800 * 1024,
  };
  rstream::rtty::client client(io_context.get_executor(), config, settings);
  signal_set.async_wait([&client](const std::error_code&, int) { client.cancel(); });
  std::pair<std::error_code, int> result;
  client.async_run([&signal_set, &result](const std::error_code& error_code, int code) {
    result = std::make_pair(error_code, code);
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
  if (result.first) {
    std::cerr << result.first.message() << std::endl;
  }
  return result.first ? -1 : result.second;
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
