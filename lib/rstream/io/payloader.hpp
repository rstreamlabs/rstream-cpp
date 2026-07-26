// See LICENSE file in the project root for license information.

#pragma once

#include <cstdint>
#include <cstring>
#include <functional>
#include <limits>
#include <list>
#include <memory>
#include <type_traits>

#include <boost/asio/associated_allocator.hpp>
#include <boost/asio/async_result.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/write.hpp>

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
  auto async_send(const core::buffer& buffer, BOOST_ASIO_MOVE_ARG(send_handler) handler);

  template <typename recv_handler>
  auto async_recv(const core::buffer& buffer, BOOST_ASIO_MOVE_ARG(recv_handler) handler);

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
auto payloader<stream>::async_send(const core::buffer& buffer, BOOST_ASIO_MOVE_ARG(send_handler) handler)
{
  return boost::asio::async_initiate<send_handler, void(const boost::system::error_code&)>(
      [this](auto&& handler, core::buffer payload) {
        const auto payload_size = payload.get_size();
        if (payload_size > std::numeric_limits<std::uint32_t>::max()) {
          rstream::core::invoke_completion_handler(
              get_executor(), std::forward<decltype(handler)>(handler),
              error::make_error_code(error::code::invalid_buffer_size));
          return;
        }
        auto header             = core::make_memory_allocated(sizeof(std::uint32_t), m_allocator);
        const auto network_size = htonl(static_cast<std::uint32_t>(payload_size));
        std::memcpy(header.get_data(), &network_size, sizeof(network_size));
        core::buffer message(m_allocator);
        message.append(header);
        message.append(payload);
        auto completion_handler = rstream::core::bind_associated_handler(
            std::forward<decltype(handler)>(handler),
            [executor = get_executor(), message](auto& handler, const boost::system::error_code& error_code, std::size_t size) mutable {
              (void)size;
              (void)message;
              rstream::core::invoke_completion_handler(executor, std::move(handler), error_code);
            });
        boost::asio::async_write(
            m_next_layer, core::helpers::const_memory_sequence(message),
            std::move(completion_handler));
      },
      handler, buffer);
}

template <class stream>
template <typename recv_handler>
auto payloader<stream>::async_recv(const core::buffer& buffer, BOOST_ASIO_MOVE_ARG(recv_handler) handler)
{
  return boost::asio::async_initiate<recv_handler, void(const boost::system::error_code&)>(
      [this](auto&& handler, const core::buffer& buffer) {
        using operation_type     = async_recv_operation<std::decay_t<decltype(handler)>>;
        auto operation_allocator = boost::asio::get_associated_allocator(handler);
        std::allocate_shared<operation_type>(
            operation_allocator,
            m_next_layer, buffer, std::forward<decltype(handler)>(handler), m_control_cb)
            ->run();
      },
      handler, buffer);
}

template <class stream>
template <typename T>
class payloader<stream>::async_recv_operation : public std::enable_shared_from_this<async_recv_operation<T>> {
 public:
  using handler_type = typename std::remove_reference<T>::type;

  async_recv_operation(stream& next_layer, const core::buffer& buffer, handler_type handler, const control_cb& control_cb);

  void run();

  void do_read_header(handler_type handler);

  void on_read_header(handler_type handler, const boost::system::error_code& error_code, std::size_t);

  void do_read_payload(handler_type handler);

  void on_read_payload(handler_type handler, const boost::system::error_code& error_code, std::size_t);

  void on_error(handler_type handler, const boost::system::error_code& error_code);

 private:
  stream& m_next_layer;

  core::buffer m_buffer;

  handler_type m_handler;

  control_cb m_control_cb;

  std::uint32_t m_header;
};

template <class stream>
template <typename T>
payloader<stream>::async_recv_operation<T>::async_recv_operation(stream& next_layer, const core::buffer& buffer, handler_type handler, const control_cb& control_cb)
    : m_next_layer(next_layer),
      m_buffer(buffer),
      m_handler(std::move(handler)),
      m_control_cb(control_cb)
{
}

template <class stream>
template <typename T>
void payloader<stream>::async_recv_operation<T>::run()
{
  do_read_header(std::move(m_handler));
}

template <class stream>
template <typename T>
void payloader<stream>::async_recv_operation<T>::do_read_header(handler_type handler)
{
  boost::asio::mutable_buffer buffer(&m_header, sizeof(std::uint32_t));
  auto ptr                = std::enable_shared_from_this<async_recv_operation<T>>::shared_from_this();
  auto completion_handler = rstream::core::bind_associated_handler(
      std::move(handler),
      [ptr](auto& handler, const boost::system::error_code& error_code, std::size_t size) { ptr->on_read_header(std::move(handler), error_code, size); });
  boost::asio::async_read(m_next_layer, buffer, std::move(completion_handler));
}

template <class stream>
template <typename T>
void payloader<stream>::async_recv_operation<T>::on_read_header(handler_type handler, const boost::system::error_code& error_code, std::size_t)
{
  if (error_code) {
    on_error(std::move(handler), error_code);
  }
  else {
    do_read_payload(std::move(handler));
  }
}

template <class stream>
template <typename T>
void payloader<stream>::async_recv_operation<T>::do_read_payload(handler_type handler)
{
  m_header = ntohl(m_header);
  if (m_control_cb) {
    try {
      m_control_cb(m_buffer, m_header);
    }
    catch (const boost::system::system_error& error) {
      on_error(std::move(handler), error.code());
      return;
    }
    catch (...) {
      on_error(std::move(handler), error::code::unknown_undefined_error);
      return;
    }
  }
  std::size_t offset, maxsize;
  m_buffer.get_size(offset, maxsize);
  if (m_header > maxsize) {
    on_error(std::move(handler), error::code::invalid_buffer_size);
  }
  else {
    m_buffer.set_size(m_header);
    auto ptr                = std::enable_shared_from_this<async_recv_operation<T>>::shared_from_this();
    auto completion_handler = rstream::core::bind_associated_handler(
        std::move(handler),
        [ptr](auto& handler, const boost::system::error_code& error_code, std::size_t size) { ptr->on_read_payload(std::move(handler), error_code, size); });
    boost::asio::async_read(m_next_layer, core::helpers::mutable_memory_sequence(m_buffer), std::move(completion_handler));
  }
}

template <class stream>
template <typename T>
void payloader<stream>::async_recv_operation<T>::on_read_payload(handler_type handler, const boost::system::error_code& error_code, std::size_t size)
{
  (void)size;
  on_error(std::move(handler), error_code);
}

template <class stream>
template <typename T>
void payloader<stream>::async_recv_operation<T>::on_error(handler_type handler, const boost::system::error_code& error_code)
{
  rstream::core::invoke_completion_handler(m_next_layer.get_executor(), std::move(handler), error_code);
}

}  // namespace io
}  // namespace rstream
