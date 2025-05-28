// See LICENSE file in the project root for license information.

#include <iostream>

#include <boost/asio/awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/local/connect_pair.hpp>
#include <boost/asio/local/stream_protocol.hpp>

#include <rstream/core/crc32.hpp>
#include <rstream/core/exception.hpp>
#include <rstream/core/log.hpp>
#include <rstream/core/memory.hpp>
#include <rstream/core/random.hpp>
#include <rstream/io/payloader.hpp>

static const std::size_t g_buffer_size = 200;
static const int g_count               = 500;

int main(int argc, char** argv)
{
  (void)argc;
  (void)argv;
  rstream::core::log::enable_ansicolor_stdout_mt();
  boost::asio::io_context io_context;
  using socket_type    = boost::asio::local::stream_protocol::socket;
  using payloader_type = rstream::io::payloader<socket_type&>;
  auto socket_a        = std::make_shared<socket_type>(io_context.get_executor());
  auto payloader_a     = std::make_shared<payloader_type>(*socket_a);
  auto socket_b        = std::make_shared<socket_type>(io_context.get_executor());
  auto payloader_b     = std::make_shared<payloader_type>(*socket_b);
  boost::asio::local::connect_pair(*socket_a, *socket_b);
  auto run_a = [socket = socket_a, payloader = payloader_a]() -> boost::asio::awaitable<void> {
    try {
      auto memory = rstream::core::make_memory_allocated(g_buffer_size);
      for (int i = 0; i < g_count; ++i) {
        memory.set_size(g_buffer_size);
        rstream::core::random_bytes(memory.get_data(), memory.get_size());
        std::uint32_t crc32_1 = rstream::core::crc32(memory.get_const_data(), memory.get_size());
        std::cout << "A: sending " << memory.get_size() << " bytes..." << std::endl;
        co_await payloader->async_send(memory, boost::asio::use_awaitable);
        std::cout << "A: message sent" << std::endl;
        memory.set_size(10);
        std::cout << "A: receiving data..." << std::endl;
        co_await payloader->async_recv(memory, boost::asio::use_awaitable);
        std::cout << "A: received " << memory.get_size() << " bytes" << std::endl;
        assert(memory.get_size() == g_buffer_size);
        std::uint32_t crc32_2 = rstream::core::crc32(memory.get_const_data(), memory.get_size());
        assert(crc32_1 == crc32_2);
      }
      std::cout << "A: closing" << std::endl;
      memory.set_size(0);
      co_await payloader->async_send(memory, boost::asio::use_awaitable);
      socket->close();
    }
    catch (...) {
      std::cout << "A: " << rstream::core::throwable::message(std::current_exception()) << std::endl;
    }
    co_return;
  };
  auto run_b = [socket = socket_b, payloader = payloader_b]() -> boost::asio::awaitable<void> {
    try {
      auto memory = rstream::core::make_memory_allocated(g_buffer_size);
      while (true) {
        memory.set_size(g_buffer_size);
        rstream::core::buffer buffer;
        buffer.append(memory);
        std::cout << "B: receiving data..." << std::endl;
        co_await payloader->async_recv(buffer, boost::asio::use_awaitable);
        std::cout << "B: received " << buffer.get_size() << " bytes" << std::endl;
        if (buffer.get_size() == 0) {
          break;
        }
        else {
          std::cout << "B: sending " << buffer.get_size() << " bytes..." << std::endl;
          co_await payloader->async_send(buffer, boost::asio::use_awaitable);
          std::cout << "B: message sent" << std::endl;
        }
      }
      std::cout << "B: closing" << std::endl;
      socket->close();
    }
    catch (...) {
      std::cout << "B: " << rstream::core::throwable::message(std::current_exception()) << std::endl;
    }
    co_return;
  };
  boost::asio::co_spawn(io_context.get_executor(), run_a, boost::asio::detached);
  boost::asio::co_spawn(io_context.get_executor(), run_b, boost::asio::detached);
  io_context.run();
  return 0;
}
