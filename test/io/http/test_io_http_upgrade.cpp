// See LICENSE file in the project root for license information.

#include <iostream>

#include <boost/asio/awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/local/connect_pair.hpp>
#include <boost/asio/local/stream_protocol.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/write.hpp>

#include <rstream/core/crc32.hpp>
#include <rstream/core/log.hpp>
#include <rstream/core/memory.hpp>
#include <rstream/core/random.hpp>
#include <rstream/io/detail/http/upgrade.hpp>

static const std::size_t g_buffer_size = 200;

int main(int argc, char** argv)
{
  (void)argc;
  (void)argv;
  rstream::core::log::enable_ansicolor_stdout_mt();
  boost::asio::io_context io_context;
  using socket_type  = boost::asio::local::stream_protocol::socket;
  using adaptor_type = rstream::io::detail::http::upgrade<socket_type&>;
  auto socket_a      = std::make_shared<socket_type>(io_context.get_executor());
  auto adaptor_a     = std::make_shared<adaptor_type>(*socket_a);
  auto socket_b      = std::make_shared<socket_type>(io_context.get_executor());
  auto adaptor_b     = std::make_shared<adaptor_type>(*socket_b);
  boost::asio::local::connect_pair(*socket_a, *socket_b);
  auto run_a = [socket = socket_a, adaptor = adaptor_a]() -> boost::asio::awaitable<void> {
    std::cout << "async handshake..." << std::endl;
    co_await adaptor->async_handshake("host", "/", boost::asio::use_awaitable);
    std::cout << "handshake OK" << std::endl;
    auto memory = rstream::core::make_memory_allocated(g_buffer_size);
    rstream::core::random_bytes(memory.get_data(), memory.get_size());
    std::uint32_t crc32_1 = rstream::core::crc32(memory.get_const_data(), memory.get_size());
    std::cout << "async write..." << std::endl;
    co_await boost::asio::async_write(*socket, boost::asio::const_buffer(memory.get_const_data(), memory.get_size()), boost::asio::use_awaitable);
    std::cout << "async write OK" << std::endl;
    std::cout << "async read..." << std::endl;
    auto size = co_await boost::asio::async_read(*socket, boost::asio::mutable_buffer(memory.get_data(), memory.get_size()), boost::asio::use_awaitable);
    std::cout << "async read OK" << std::endl;
    assert(size == g_buffer_size);
    std::uint32_t crc32_2 = rstream::core::crc32(memory.get_const_data(), memory.get_size());
    assert(crc32_1 == crc32_2);
    socket->close();
    co_return;
  };
  auto run_b = [socket = socket_b, adaptor = adaptor_b]() -> boost::asio::awaitable<void> {
    co_await adaptor->async_accept(boost::asio::use_awaitable);
    auto memory = rstream::core::make_memory_allocated(g_buffer_size);
    auto size   = co_await boost::asio::async_read(*socket, boost::asio::mutable_buffer(memory.get_data(), memory.get_size()), boost::asio::use_awaitable);
    memory.set_size(size);
    co_await boost::asio::async_write(*socket, boost::asio::const_buffer(memory.get_const_data(), memory.get_size()), boost::asio::use_awaitable);
    socket->close();
    co_return;
  };
  boost::asio::co_spawn(io_context.get_executor(), run_a, boost::asio::detached);
  boost::asio::co_spawn(io_context.get_executor(), run_b, boost::asio::detached);
  io_context.run();
  return 0;
}
