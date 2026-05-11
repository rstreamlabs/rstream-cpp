// See LICENSE file in the project root for license information.

#include <cassert>

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/local/connect_pair.hpp>
#include <boost/asio/local/stream_protocol.hpp>

#include <rstream/core/buffer.hpp>
#include <rstream/io/error.hpp>
#include <rstream/io/payloader.hpp>

static void check_payload_larger_than_buffer_is_rejected()
{
  boost::asio::io_context io_context;
  using socket_type    = boost::asio::local::stream_protocol::socket;
  using payloader_type = rstream::io::payloader<socket_type&>;
  auto socket_a        = std::make_shared<socket_type>(io_context.get_executor());
  auto socket_b        = std::make_shared<socket_type>(io_context.get_executor());
  boost::asio::local::connect_pair(*socket_a, *socket_b);
  payloader_type sender(*socket_a);
  payloader_type receiver(*socket_b);
  auto send_buffer = rstream::core::make_buffer_allocated(16);
  auto recv_buffer = rstream::core::make_buffer_allocated(4);
  bool sender_called = false;
  bool receiver_called = false;
  sender.async_send(send_buffer, [&](const boost::system::error_code& error_code) {
    sender_called = true;
    assert(!error_code);
  });
  receiver.async_recv(recv_buffer, [&](const boost::system::error_code& error_code) {
    receiver_called = true;
    assert(error_code.value() == static_cast<int>(rstream::io::error::code::invalid_buffer_size));
  });
  io_context.run();
  assert(sender_called);
  assert(receiver_called);
}

int main(int argc, char** argv)
{
  (void)argc;
  (void)argv;
  check_payload_larger_than_buffer_is_rejected();
  return 0;
}
