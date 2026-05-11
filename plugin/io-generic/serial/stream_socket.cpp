// See LICENSE file in the project root for license information.

#include "stream_socket.hpp"

#ifdef _WIN32
#include <windows.h>
#else
#include <termios.h>
#include <unistd.h>
#endif

#include <boost/asio/async_result.hpp>
#include <boost/asio/dispatch.hpp>

#include <rstream/core/completion_handler.hpp>
#include <rstream/io/error.hpp>

namespace rstream {
namespace plugin {
namespace io_generic {
namespace serial {

}
}  // namespace io_generic
}  // namespace plugin
}  // namespace rstream

template <>
void rstream::plugin::io_generic::serial::stream_socket::open_internal(const rstream::plugin::io_generic::serial::descriptor& endpoint, boost::system::error_code& error_code)
{
  m_socket.open(endpoint.m_device, error_code);
}

template <>
void rstream::plugin::io_generic::serial::stream_socket::configure_internal(rstream::io::detail::stream::socket_mode mode, bool connected, const rstream::plugin::io_generic::serial::descriptor& endpoint, const boost::urls::url& url, boost::system::error_code& error_code)
{
  if (connected) {
    return;
  }
  (void)mode;
  if (!m_socket.is_open()) {
    open_internal(endpoint, error_code);
    if (error_code) {
      return;
    }
  }
  m_socket.set_option(boost::asio::serial_port_base::baud_rate(endpoint.m_baudrate), error_code);
  if (error_code) {
    return;
  }
  m_socket.set_option(boost::asio::serial_port_base::flow_control(boost::asio::serial_port_base::flow_control::none), error_code);
  if (error_code) {
    return;
  }
  m_socket.set_option(boost::asio::serial_port_base::parity(boost::asio::serial_port_base::parity::none), error_code);
  if (error_code) {
    return;
  }
  m_socket.set_option(boost::asio::serial_port_base::stop_bits(boost::asio::serial_port_base::stop_bits::one), error_code);
  if (error_code) {
    return;
  }
  m_socket.set_option(boost::asio::serial_port_base::character_size(8), error_code);
  if (error_code) {
    return;
  }
#ifndef _WIN32
  enum flush_type {
    flush_receive = TCIFLUSH,
    flush_send    = TCOFLUSH,
    flush_both    = TCIOFLUSH
  };
  auto flush = [](boost::asio::serial_port& socket, flush_type what, boost::system::error_code& error_code) {
    if (::tcflush(socket.lowest_layer().native_handle(), what) != 0) {
      error_code = boost::system::error_code(errno, boost::asio::error::get_system_category());
    }
  };
  flush(m_socket, flush_both, error_code);
#else
  HANDLE handle = m_socket.lowest_layer().native_handle();
  if (!::PurgeComm(handle, PURGE_TXCLEAR | PURGE_RXCLEAR)) {
    error_code = boost::system::error_code(::GetLastError(), boost::asio::error::get_system_category());
  }
#endif
}

template <>
rstream::plugin::io_generic::serial::descriptor rstream::plugin::io_generic::serial::stream_socket::remote_endpoint_internal(boost::system::error_code& error_code)
{
  rstream::plugin::io_generic::serial::descriptor result = {};
  error_code                                             = io::error::code::unsupported_operation;
  return result;
}

template <>
void rstream::plugin::io_generic::serial::stream_socket::async_connect_internal(const rstream::plugin::io_generic::serial::descriptor& endpoint, async_connect_completion_handler&& handler)
{
  rstream::core::invoke_completion_handler(get_executor(), std::move(handler), boost::system::error_code());
}
