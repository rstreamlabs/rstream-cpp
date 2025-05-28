// See LICENSE file in the project root for license information.

#pragma once

#include <cstdint>
#include <functional>
#include <list>
#include <memory>
#include <type_traits>

#include <boost/asio/async_result.hpp>
#include <boost/asio/bind_executor.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/asio/consign.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/write.hpp>
#include <boost/bind/bind.hpp>
#include <boost/optional.hpp>

#include <rstream/core/allocator.hpp>
#include <rstream/core/buffer.hpp>
#include <rstream/core/completion_handler.hpp>
#include <rstream/core/exception.hpp>
#include <rstream/core/helpers/asio.hpp>

#include "error.hpp"

namespace rstream {
namespace io {

template <class stream>
class payloader {
 public:
  template <class T>
  using container = std::list<T, ::rstream::core::allocator::wrapper<T>>;

  using next_layer_type = typename std::remove_reference<stream>::type;

  using executor_type = typename next_layer_type::executor_type;

  template <typename arg_type>
  payloader(arg_type&& arg, ::rstream::core::allocator::ptr allocator = nullptr)
      : m_next_layer(BOOST_ASIO_MOVE_CAST(arg_type)(arg)),
        m_allocator(allocator)
  {
  }

  template <typename arg_type>
  payloader(arg_type& arg, ::rstream::core::allocator::ptr allocator = nullptr)
      : m_next_layer(arg),
        m_allocator(allocator)
  {
  }

  next_layer_type& next_layer()
  {
    return m_next_layer;
  }

  const next_layer_type& next_layer() const
  {
    return m_next_layer;
  }

  executor_type get_executor() const
  {
    return m_next_layer.get_executor();
  }

  template <typename send_handler>
  BOOST_ASIO_INITFN_RESULT_TYPE(BOOST_ASIO_MOVE_ARG(send_handler), void(const boost::system::error_code&))
  async_send(const core::buffer& buffer, BOOST_ASIO_MOVE_ARG(send_handler) handler);

  template <typename recv_handler>
  BOOST_ASIO_INITFN_RESULT_TYPE(BOOST_ASIO_MOVE_ARG(recv_handler), void(const boost::system::error_code&))
  async_recv(const core::buffer& buffer, BOOST_ASIO_MOVE_ARG(recv_handler) handler);

  using control_cb = std::function<void(core::buffer&, std::size_t)>;
  void set_control_callback(const control_cb& control_cb)
  {
    m_control_cb = control_cb;
  }

 private:
  template <typename T>
  class async_recv_operation;

  stream m_next_layer;

  ::rstream::core::allocator::ptr m_allocator;

  control_cb m_control_cb;
};

template <class stream>
template <typename send_handler>
BOOST_ASIO_INITFN_RESULT_TYPE(BOOST_ASIO_MOVE_ARG(send_handler), void(const boost::system::error_code&))
payloader<stream>::async_send(const core::buffer& buffer, BOOST_ASIO_MOVE_ARG(send_handler) handler)
{
  auto header                          = core::make_memory_allocated(sizeof(std::uint32_t), m_allocator);
  *((std::uint32_t*)header.get_data()) = htonl(static_cast<std::uint32_t>(buffer.get_size()));
  core::buffer msg(m_allocator);
  msg.append(header);
  msg.append(buffer);
  return boost::asio::async_initiate<send_handler, void(const boost::system::error_code&)>(
      [this](auto&& handler, const core::buffer buffer) {
        boost::asio::async_write(
            m_next_layer, core::helpers::const_memory_sequence(buffer),
            [executor = get_executor(), handler = std::move(handler), buffer](const boost::system::error_code& error_code, std::size_t size) mutable {
              (void)size;
              (void)buffer;
              rstream::core::invoke_completion_handler(executor, std::move(handler), error_code);
            });
      },
      handler, msg);
}

template <class stream>
template <typename recv_handler>
BOOST_ASIO_INITFN_RESULT_TYPE(BOOST_ASIO_MOVE_ARG(recv_handler), void(const boost::system::error_code&))
payloader<stream>::async_recv(const core::buffer& buffer, BOOST_ASIO_MOVE_ARG(recv_handler) handler)
{
  return boost::asio::async_initiate<recv_handler, void(const boost::system::error_code&)>(
      [this](auto&& handler, const core::buffer& buffer) {
        std::allocate_shared<async_recv_operation<decltype(handler)>>(core::allocator::wrapper<async_recv_operation<decltype(handler)>>(m_allocator), m_next_layer, buffer, std::forward<decltype(handler)>(handler), m_control_cb)->run();
      },
      handler, buffer);
}

template <class stream>
template <typename T>
class payloader<stream>::async_recv_operation : public std::enable_shared_from_this<async_recv_operation<T>> {
 public:
  using handler_type = typename std::remove_reference<T>::type;

  async_recv_operation(stream& next_layer, const core::buffer& buffer, T&& handler, const control_cb& control_cb);

  void run();

  void do_read_header();

  void on_read_header(const boost::system::error_code& error_code, std::size_t);

  void do_read_payload();

  void on_read_payload(const boost::system::error_code& error_code, std::size_t);

  void on_error(const boost::system::error_code& error_code);

 private:
  stream& m_next_layer;

  core::buffer m_buffer;

  handler_type m_handler;

  control_cb m_control_cb;

  std::uint32_t m_header;
};

template <class stream>
template <typename T>
payloader<stream>::async_recv_operation<T>::async_recv_operation(stream& next_layer, const core::buffer& buffer, T&& handler, const control_cb& control_cb)
    : m_next_layer(next_layer),
      m_buffer(buffer),
      m_handler(std::forward<decltype(handler)>(handler)),
      m_control_cb(control_cb)

{
}

template <class stream>
template <typename T>
void payloader<stream>::async_recv_operation<T>::run()
{
  do_read_header();
}

template <class stream>
template <typename T>
void payloader<stream>::async_recv_operation<T>::do_read_header()
{
  boost::asio::mutable_buffer buffer(&m_header, sizeof(std::uint32_t));
  auto ptr     = std::enable_shared_from_this<async_recv_operation<T>>::shared_from_this();
  auto handler = std::bind(&async_recv_operation<T>::on_read_header, ptr, std::placeholders::_1, std::placeholders::_2);
  boost::asio::async_read(m_next_layer, buffer, handler);
}

template <class stream>
template <typename T>
void payloader<stream>::async_recv_operation<T>::on_read_header(const boost::system::error_code& error_code, std::size_t)
{
  if (error_code) {
    on_error(error_code);
  }
  else {
    do_read_payload();
  }
}

template <class stream>
template <typename T>
void payloader<stream>::async_recv_operation<T>::do_read_payload()
{
  m_header = ntohl(m_header);
  if (m_control_cb) {
    m_control_cb(m_buffer, m_header);
  }
  std::size_t offset, maxsize;
  m_buffer.get_size(offset, maxsize);
  if (m_header > maxsize) {
    on_error(error::code::invalid_buffer_size);
  }
  else {
    m_buffer.set_size(m_header);
    auto ptr     = std::enable_shared_from_this<async_recv_operation<T>>::shared_from_this();
    auto handler = std::bind(&async_recv_operation<T>::on_read_payload, ptr, std::placeholders::_1, std::placeholders::_2);
    boost::asio::async_read(m_next_layer, core::helpers::mutable_memory_sequence(m_buffer), handler);
  }
}

template <class stream>
template <typename T>
void payloader<stream>::async_recv_operation<T>::on_read_payload(const boost::system::error_code& error_code, std::size_t size)
{
  (void)size;
  on_error(error_code);
}

template <class stream>
template <typename T>
void payloader<stream>::async_recv_operation<T>::on_error(const boost::system::error_code& error_code)
{
  rstream::core::invoke_completion_handler(m_next_layer.get_executor(), std::move(m_handler), error_code);
}

}  // namespace io
}  // namespace rstream
