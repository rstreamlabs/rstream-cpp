// See LICENSE file in the project root for license information.

#pragma once

#include <vector>

#include <boost/asio/async_result.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/iterator/distance.hpp>
#include <boost/system/error_code.hpp>

#include <rstream/core/completion_handler.hpp>

#include "socket_base.hpp"

namespace rstream {
namespace io {

class stream_socket_base_interface {
 public:
  template <class U>
  using buffer_sequence_type = std::vector<U>;

  using const_buffer_sequence_type = buffer_sequence_type<boost::asio::const_buffer>;

  using mutable_buffer_sequence_type = buffer_sequence_type<boost::asio::mutable_buffer>;

  stream_socket_base_interface(const io_object::executor_type& executor);

  using async_write_some_completion_handler = rstream::core::completion_handler<void(const boost::system::error_code&, std::size_t)>;
  template <typename const_buffer_sequence, typename write_handler>
  BOOST_ASIO_INITFN_RESULT_TYPE(BOOST_ASIO_MOVE_ARG(write_handler), void(const boost::system::error_code&, std::size_t))
  async_write_some(const const_buffer_sequence& buffers, BOOST_ASIO_MOVE_ARG(write_handler) handler)
  {
    return boost::asio::async_initiate<write_handler, void(const boost::system::error_code&, std::size_t)>(async_write_some_op<const_buffer_sequence>(*this), handler, buffers);
  }

  using async_read_some_completion_handler = rstream::core::completion_handler<void(const boost::system::error_code&, std::size_t)>;
  template <typename mutable_buffer_sequence, typename read_handler>
  BOOST_ASIO_INITFN_RESULT_TYPE(BOOST_ASIO_MOVE_ARG(read_handler), void(const boost::system::error_code&, std::size_t))
  async_read_some(const mutable_buffer_sequence& buffers, BOOST_ASIO_MOVE_ARG(read_handler) handler)
  {
    return boost::asio::async_initiate<read_handler, void(const boost::system::error_code&, std::size_t)>(async_read_some_op<mutable_buffer_sequence>(*this), handler, buffers);
  }

 private:
  template <typename const_buffer_sequence>
  struct async_write_some_op {
    async_write_some_op(stream_socket_base_interface& socket)
        : m_socket(socket)
    {
    }
    template <typename write_handler>
    void operator()(BOOST_ASIO_MOVE_ARG(write_handler) handler, const const_buffer_sequence& buffers)
    {
      auto begin = boost::asio::buffer_sequence_begin(buffers);
      auto end   = boost::asio::buffer_sequence_end(buffers);
      if (begin == end) {
        rstream::core::invoke_completion_handler(m_socket.m_executor, std::move(handler), boost::system::error_code(), 0);
      }
      else {
        const auto count = boost::iterators::distance(begin, end);
        if (count == 1) {
          m_socket.async_write_some_internal(*begin, std::forward<decltype(handler)>(handler));
        }
        else {
          buffer_sequence_type<boost::asio::const_buffer> buffer;
          buffer.reserve(count);
          for (auto it = begin; it != end; ++it) {
            buffer.push_back(*it);
          }
          m_socket.async_write_some_internal(buffer, std::forward<decltype(handler)>(handler));
        }
      }
    }
    stream_socket_base_interface& m_socket;
  };

  template <typename mutable_buffer_sequence>
  struct async_read_some_op {
    async_read_some_op(stream_socket_base_interface& socket)
        : m_socket(socket)
    {
    }
    template <typename read_handler>
    void operator()(BOOST_ASIO_MOVE_ARG(read_handler) handler, const mutable_buffer_sequence& buffers)
    {
      auto begin = boost::asio::buffer_sequence_begin(buffers);
      auto end   = boost::asio::buffer_sequence_end(buffers);
      if (begin == end) {
        rstream::core::invoke_completion_handler(m_socket.m_executor, std::move(handler), boost::system::error_code(), 0);
      }
      else {
        const auto count = boost::iterators::distance(begin, end);
        if (count == 1) {
          m_socket.async_read_some_internal(*begin, std::forward<decltype(handler)>(handler));
        }
        else {
          buffer_sequence_type<boost::asio::mutable_buffer> buffer;
          buffer.reserve(count);
          for (auto it = begin; it != end; ++it) {
            buffer.push_back(*it);
          }
          m_socket.async_read_some_internal(buffer, std::forward<decltype(handler)>(handler));
        }
      }
    }
    stream_socket_base_interface& m_socket;
  };

  virtual void async_write_some_internal(const boost::asio::const_buffer& buffer, async_write_some_completion_handler&& handler) = 0;

  virtual void async_write_some_internal(const const_buffer_sequence_type& buffer, async_write_some_completion_handler&& handler) = 0;

  virtual void async_read_some_internal(const boost::asio::mutable_buffer& buffer, async_read_some_completion_handler&& handler) = 0;

  virtual void async_read_some_internal(const mutable_buffer_sequence_type& buffer, async_read_some_completion_handler&& handler) = 0;

  io_object::executor_type m_executor;
};

template <>
struct stream_socket_base_interface::async_write_some_op<boost::asio::const_buffer> {
  async_write_some_op(stream_socket_base_interface& socket);
  template <typename write_handler>
  void operator()(BOOST_ASIO_MOVE_ARG(write_handler) handler, const boost::asio::const_buffer& buffer)
  {
    m_socket.async_write_some_internal(buffer, std::forward<decltype(handler)>(handler));
  }
  stream_socket_base_interface& m_socket;
};

template <>
struct stream_socket_base_interface::async_write_some_op<stream_socket_base_interface::const_buffer_sequence_type> {
  async_write_some_op(stream_socket_base_interface& socket);
  template <typename write_handler>
  void operator()(BOOST_ASIO_MOVE_ARG(write_handler) handler, const const_buffer_sequence_type& buffer)
  {
    m_socket.async_write_some_internal(buffer, std::forward<decltype(handler)>(handler));
  }
  stream_socket_base_interface& m_socket;
};

template <>
struct stream_socket_base_interface::async_read_some_op<boost::asio::mutable_buffer> {
  async_read_some_op(stream_socket_base_interface& socket);
  template <typename read_handler>
  void operator()(BOOST_ASIO_MOVE_ARG(read_handler) handler, const boost::asio::mutable_buffer& buffer)
  {
    m_socket.async_read_some_internal(buffer, std::forward<decltype(handler)>(handler));
  }
  stream_socket_base_interface& m_socket;
};

template <>
struct stream_socket_base_interface::async_read_some_op<stream_socket_base_interface::mutable_buffer_sequence_type> {
  async_read_some_op(stream_socket_base_interface& socket);
  template <typename read_handler>
  void operator()(BOOST_ASIO_MOVE_ARG(read_handler) handler, const mutable_buffer_sequence_type& buffer)
  {
    m_socket.async_read_some_internal(buffer, std::forward<decltype(handler)>(handler));
  }
  stream_socket_base_interface& m_socket;
};

template <class T>
class stream_socket_base : public socket_base<T>, public stream_socket_base_interface {
 public:
  using endpoint_type = T;

  stream_socket_base(const io_object::executor_type& executor);

  virtual ~stream_socket_base() = default;

  virtual endpoint_type remote_endpoint(boost::system::error_code& error_code) = 0;

  endpoint_type remote_endpoint();

  virtual bool is_secure() const;

  using async_connect_completion_handler = rstream::core::completion_handler<void(const boost::system::error_code&)>;
  template <typename connect_handler>
  BOOST_ASIO_INITFN_RESULT_TYPE(BOOST_ASIO_MOVE_ARG(connect_handler), void(const boost::system::error_code&))
  async_connect(const endpoint_type& endpoint, BOOST_ASIO_MOVE_ARG(connect_handler) handler)
  {
    return boost::asio::async_initiate<connect_handler, void(const boost::system::error_code&)>(async_connect_op(*this), handler, endpoint);
  }

 private:
  struct async_connect_op {
    async_connect_op(stream_socket_base& socket)
        : m_socket(socket)
    {
    }
    template <typename connect_handler>
    void operator()(BOOST_ASIO_MOVE_ARG(connect_handler) handler, const endpoint_type& endpoint)
    {
      m_socket.async_connect_internal(endpoint, std::forward<decltype(handler)>(handler));
    }
    stream_socket_base& m_socket;
  };

  virtual void async_connect_internal(const endpoint_type& endpoint, async_connect_completion_handler&& handler) = 0;
};

template <class T>
stream_socket_base<T>::stream_socket_base(const io_object::executor_type& executor)
    : socket_base<T>(executor),
      stream_socket_base_interface(executor)
{
}

template <class T>
typename stream_socket_base<T>::endpoint_type stream_socket_base<T>::remote_endpoint()
{
  endpoint_type endpoint;
  boost::system::error_code error_code;
  endpoint = remote_endpoint(error_code);
  if (error_code) {
    throw boost::system::system_error(error_code);
  }
  return endpoint;
}

template <class T>
bool stream_socket_base<T>::is_secure() const
{
  return false;
}

}  // namespace io
}  // namespace rstream
