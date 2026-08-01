// See LICENSE file in the project root for license information.

#include <chrono>
#include <cstdint>
#include <iostream>
#include <sstream>
#include <thread>

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/experimental/awaitable_operators.hpp>
#include <boost/asio/experimental/parallel_group.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/asio/write.hpp>

#include <docopt.h>

#include <rstream/config.hpp>
#include <rstream/core/exception.hpp>
#include <rstream/core/log.hpp>
#include <rstream/core/memory.hpp>
#include <rstream/core/random.hpp>
#include <rstream/io-rstrm/client.hpp>
#include <rstream/io-rstrm/error.hpp>

static const char USAGE[] = R"(
rstream-example-io-rstrm-client

usage:
  rstream-example-io-rstrm-client [options]
  rstream-example-io-rstrm-client (-h|--help)
  rstream-example-io-rstrm-client --version

example:
  rstream-example-io-rstrm-client -v -j 1 -t 1 -c 1 -w 1024 -b 1024
  rstream-example-io-rstrm-client -v -j 0 -t 8 -c 4 -w 1024 -b 1024

options:
  -h --help            show this screen
  --version            show version
  -v --verbose         enable verbose mode
  -j --jobs=ARG        number of threads to run simultaneously (0 = auto) [default: 0]
  -s --server=ARG      server address
  -t --tunnels=ARG     number of tunnels [default: 5]
  -c --clients=ARG     number of clients [default: 5]
  -w --write=ARG       write count [default: 1024]
  -b --buffer=ARG      buffer size [default: 1024]
)";

struct config {
  std::size_t m_tunnels_count;
  std::size_t m_clients_count;
  std::size_t m_write_count;
  std::size_t m_buffer_size;
};

static config g_config;

const auto version = std::string("rstream-example-io-rstrm-client ") + RSTREAM_VERSION;

using namespace boost::asio::experimental::awaitable_operators;

boost::asio::awaitable<void> coro_session(rstream::io_rstrm::socket socket)
{
  std::cout << "[session] starting session coroutine" << std::endl;
  try {
    auto buffer = rstream::core::make_memory_allocated(g_config.m_buffer_size);
    for (;;) {
      std::size_t n = co_await socket.async_read_some(boost::asio::buffer(buffer.get_data(), g_config.m_buffer_size), boost::asio::use_awaitable);
      if (n == 0) {
        break;
      }
      std::cout << "[session] echo data..." << std::endl;
      co_await boost::asio::async_write(socket, boost::asio::buffer(buffer.get_const_data(), n), boost::asio::use_awaitable);
    }
  }
  catch (const std::exception& e) {
    std::cerr << "[session] an error occured when processing the session: " << e.what() << std::endl;
  }
  std::cout << "[session] session coroutine finished" << std::endl;
  co_return;
}

boost::asio::awaitable<void> coro_listener(rstream::io_rstrm::tunnel& tunnel)
{
  std::cout << "[listener] starting listener coroutine" << std::endl;
  auto executor = co_await boost::asio::this_coro::executor;
  rstream::io_rstrm::socket socket(executor);
  try {
    {
      boost::system::error_code error_code;
      const auto& endpoint = tunnel.local_endpoint(error_code);
      if (error_code) {
        throw boost::system::system_error(error_code);
      }
      else {
        std::cout << "[listener] tunnel started on: " << endpoint << std::endl;
      }
    }
    std::cout << "[listener] listening for incoming connections..." << std::endl;
    while (true) {
      rstream::io_rstrm::endpoint peer;
      try {
        co_await tunnel.async_accept(socket, peer, boost::asio::use_awaitable);
      }
      catch (const boost::system::system_error& error) {
        if (error.code() == rstream::io_rstrm::error::code::tunnel_not_found) {
          break;
        }
        else {
          throw error;
        }
      }
      std::cout << "[listener] accepted connection from: " << peer << std::endl;
      boost::asio::co_spawn(executor, coro_session(std::move(socket)), boost::asio::detached);
    }
  }
  catch (const std::exception& e) {
    std::cerr << "[listener] an error occured when running the tunnel: " << e.what() << std::endl;
  }
  std::cout << "[listener] listener coroutine finished" << std::endl;
  co_return;
}

boost::asio::awaitable<void> coro_client(const rstream::io_rstrm::endpoint& endpoint, std::string id)
{
  std::cout << "[client] [ID " << id << "] starting client coroutine" << std::endl;
  auto executor = co_await boost::asio::this_coro::executor;
  rstream::io_rstrm::socket socket(executor);
  try {
    std::cout << "[client] [ID " << id << "] connecting to '" << endpoint << "'..." << std::endl;
    co_await socket.async_connect(endpoint, boost::asio::use_awaitable);
    std::cout << "[client] [ID " << id << "] connected to '" << endpoint << "'" << std::endl;
    auto buffer_wr = rstream::core::make_memory_allocated(g_config.m_buffer_size);
    auto buffer_rd = rstream::core::make_memory_allocated(g_config.m_buffer_size);
    for (std::size_t i = 0; i < g_config.m_write_count; ++i) {
      rstream::core::random_bytes(buffer_wr.get_data(), g_config.m_buffer_size);
      std::cout << "[client] [ID " << id << "] writing data..." << std::endl;
      co_await boost::asio::async_write(socket, boost::asio::buffer(buffer_wr.get_const_data(), g_config.m_buffer_size), boost::asio::use_awaitable);
      std::cout << "[client] [ID " << id << "] data written successfully" << std::endl;
      std::cout << "[client] [ID " << id << "] reading data..." << std::endl;
      std::size_t n = co_await boost::asio::async_read(socket, boost::asio::buffer(buffer_rd.get_data(), g_config.m_buffer_size), boost::asio::use_awaitable);
      if (n != g_config.m_buffer_size) {
        throw std::runtime_error("invalid number of bytes read");
      }
      if (std::memcmp(buffer_wr.get_const_data(), buffer_rd.get_const_data(), g_config.m_buffer_size) != 0) {
        throw std::runtime_error("invalid data read");
      }
      std::cout << "[client] [ID " << id << "] data read successfully" << std::endl;
    }
    std::cout << "[client] [ID " << id << "] writing empty data..." << std::endl;
    co_await boost::asio::async_write(socket, boost::asio::const_buffer(nullptr, 0), boost::asio::use_awaitable);
    std::cout << "[client] [ID " << id << "] empty data written successfully" << std::endl;
    std::cout << "[client] [ID " << id << "] closing connection..." << std::endl;
    {
      boost::system::error_code tmp;
      socket.close(tmp);
    }
    std::cout << "[client] [ID " << id << "] connection closed" << std::endl;
  }
  catch (const std::exception& e) {
    std::cerr << "[client] [ID " << id << "] an error occured when running the client: " << e.what() << std::endl;
  }
  std::cout << "[client] [ID " << id << "] client coroutine finished" << std::endl;
  co_return;
}

boost::asio::awaitable<void> coro_tunnel(rstream::io_rstrm::client& client, std::string name)
{
  std::cout << "[tunnel] starting tunnel coroutine" << std::endl;
  rstream::io_rstrm::endpoint endpoint;
  {
    boost::system::error_code error_code;
    const auto& address = client.address(error_code);
    if (error_code) {
      throw boost::system::system_error(error_code);
    }
    else {
      endpoint = rstream::io_rstrm::endpoint({
          .m_id_name                       = name,
          .m_server_address                = address,
          .m_server_address_from_uri_param = false,
          .m_secret                        = boost::none,
          .m_source_ip                     = boost::none,
      });
    }
  }
  auto executor = co_await boost::asio::this_coro::executor;
  std::cout << "[tunnel] creating tunnel '" << name << "'..." << std::endl;
  struct rstream::io_rstrm::tunnel_properties properties = {
      .m_name     = name,
      .m_protocol = rstream::io_rstrm::protocol::tls,  // plain TLS tunnel
  };
  auto tunnel = co_await client.async_create_tunnel(properties, boost::asio::use_awaitable);
  std::cout << "[tunnel] tunnel '" << name << "' created" << std::endl;
  auto await_listener = boost::asio::co_spawn(executor, coro_listener(tunnel), boost::asio::use_awaitable);
  using op_type       = decltype(boost::asio::co_spawn(executor, coro_client(endpoint, std::string("")), boost::asio::deferred));
  std::vector<op_type> ops;
  ops.reserve(g_config.m_clients_count);
  for (std::size_t i = 0; i < g_config.m_clients_count; ++i) {
    ops.emplace_back(boost::asio::co_spawn(executor, coro_client(endpoint, std::to_string(i)), boost::asio::deferred));
  }
  co_await (std::move(await_listener) && [&, ops = std::move(ops)]() mutable -> boost::asio::awaitable<void> {
    std::cout << "[tunnel] waiting for all clients to finish..." << std::endl;
    co_await boost::asio::experimental::make_parallel_group(std::move(ops)).async_wait(boost::asio::experimental::wait_for_all(), boost::asio::use_awaitable);
    std::cout << "[tunnel] all clients finished" << std::endl;
    std::cout << "[tunnel] closing tunnel '" << name << "'..." << std::endl;
    tunnel.close();
    std::cout << "[tunnel] tunnel '" << name << "' closed" << std::endl;
  }());
  std::cout << "[tunnel] tunnel coroutine finished" << std::endl;
  co_return;
}

boost::asio::awaitable<void> coro_main(const std::string& uri)
{
  std::cout << "[main] starting main coroutine" << std::endl;
  auto executor = co_await boost::asio::this_coro::executor;
  rstream::io_rstrm::client client(executor);
  std::cout << "[main] connecting to engine..." << std::endl;
  co_await client.async_connect(uri, boost::asio::use_awaitable);
  {
    boost::system::error_code error_code;
    const auto& address = client.address(error_code);
    if (error_code) {
      throw boost::system::system_error(error_code);
    }
    else {
      std::cout << "[main] connected to '" << address << "'" << std::endl;
    }
  }
  using op_type = decltype(boost::asio::co_spawn(executor, coro_tunnel(client, std::string("")), boost::asio::deferred));
  std::vector<op_type> ops;
  ops.reserve(g_config.m_tunnels_count);
  for (std::size_t i = 0; i < g_config.m_tunnels_count; ++i) {
    ops.emplace_back(boost::asio::co_spawn(executor, coro_tunnel(client, std::string("test-") + std::to_string(i)), boost::asio::deferred));
  }
  co_await boost::asio::experimental::make_parallel_group(std::move(ops)).async_wait(boost::asio::experimental::wait_for_all(), boost::asio::use_awaitable);
  std::cout << "[main] all tunnels closed" << std::endl;
  std::cout << "[main] cleaning up ressources..." << std::endl;
  client.close();
  std::cout << "[main] main coroutine finished" << std::endl;
  co_return;
}

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
  g_config.m_tunnels_count = args.at("--tunnels").asLong();
  g_config.m_clients_count = args.at("--clients").asLong();
  g_config.m_write_count   = args.at("--write").asLong();
  g_config.m_buffer_size   = args.at("--buffer").asLong();
  boost::asio::io_context io_context(jobs);
  boost::asio::signal_set signal_set(io_context, SIGINT, SIGTERM);
  auto handler = [&io_context](const std::exception_ptr exception_ptr) {
    if (exception_ptr) {
      std::cerr << "an error occured: " << rstream::core::throwable::to_string(exception_ptr) << std::endl;
    }
    io_context.stop();
  };
  signal_set.async_wait(std::bind(handler, nullptr));
  boost::asio::co_spawn(io_context, coro_main(args.at("--server").asString()), handler);
  std::cout << "starting io_context with " << jobs << " threads..." << std::endl;
  std::vector<std::thread> threads;
  threads.reserve(jobs);
  if (jobs > 1) {
    auto n = jobs - 1;
    for (decltype(n) i = 0; i < n; ++i) {
      threads.emplace_back(std::bind((boost::asio::io_context::count_type (boost::asio::io_context::*)())&boost::asio::io_context::run, &io_context));
    }
  }
  io_context.run();
  for (auto& thread : threads) {
    thread.join();
  }
  threads.clear();
  std::cout << "io_context stopped" << std::endl;
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
