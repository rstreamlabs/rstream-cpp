// See LICENSE file in the project root for license information.

#pragma once

#include <deque>
#include <memory>
#include <queue>

#include <boost/asio/associated_executor.hpp>
#include <boost/asio/async_result.hpp>
#include <boost/asio/bind_executor.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/strand.hpp>
#include <boost/beast/websocket/stream.hpp>
#include <boost/core/noncopyable.hpp>

#include <rstream/core/allocator.hpp>
#include <rstream/core/buffer.hpp>
#include <rstream/core/completion_handler.hpp>
#include <rstream/core/helpers/asio.hpp>
#include <rstream/io/error.hpp>

#include "error.hpp"

namespace rstream {
namespace io {

class queue_base {
 public:
  using ptr = std::shared_ptr<queue_base>;

  using async_send_completion_handler = rstream::core::completion_handler<void(const boost::system::error_code&)>;
  template <typename send_handler>
  BOOST_ASIO_INITFN_RESULT_TYPE(BOOST_ASIO_MOVE_ARG(send_handler), void(const boost::system::error_code&))
  async_send(const core::buffer buffer, BOOST_ASIO_MOVE_ARG(send_handler) handler)
  {
    return boost::asio::async_initiate<send_handler, void(const boost::system::error_code&)>(
        [this](auto&& handler, const core::buffer buffer) {
          async_send_internal(buffer, std::forward<decltype(handler)>(handler));
        },
        handler, buffer);
  }

  virtual void cancel() = 0;

 private:
  virtual void async_send_internal(const core::buffer buffer, async_send_completion_handler&& handler) = 0;
};

template <class transport>
class queue : public queue_base {
 public:
  template <class T>
  using container = std::queue<T, std::deque<T, core::allocator::wrapper<T>>>;

  using next_layer_type = typename std::remove_reference<transport>::type;

  using executor_type = typename next_layer_type::executor_type;

  template <typename arg_type>
  queue(arg_type&& arg, core::allocator::ptr allocator = nullptr);

  template <typename arg_type>
  queue(arg_type& arg, core::allocator::ptr allocator = nullptr);

  next_layer_type& next_layer();

  const next_layer_type& next_layer() const;

  executor_type get_executor() const;

  void cancel() override;

 private:
  void async_send_internal(const core::buffer buffer, async_send_completion_handler&& handler) override;

  class impl;

  std::shared_ptr<impl> m_impl;
};

template <class transport>
class queue<transport>::impl : public std::enable_shared_from_this<impl> {
 public:
  template <typename arg_type>
  impl(arg_type&& arg, core::allocator::ptr allocator);

  template <typename arg_type>
  impl(arg_type& arg, core::allocator::ptr allocator);

  next_layer_type& next_layer();

  const next_layer_type& next_layer() const;

  executor_type get_executor() const;

  void cancel();

  void async_send(const core::buffer buffer, async_send_completion_handler&& handler);

 private:
  struct task;

  void cancel_internal();

  void send(typename task::ptr task);

  void process(typename task::ptr task);

  void on_send(const boost::system::error_code& error_code);

  /// transport layer
  transport m_next_layer;

  /// we use a strand for asynchronous operations
  boost::asio::strand<executor_type> m_strand;

  /// alocator used for memory allocation
  core::allocator::ptr m_allocator;

  /// task being processed if any
  typename task::ptr m_current;

  /// task queue
  container<typename task::ptr> m_queue;
};

template <class transport>
struct queue<transport>::impl::task : boost::noncopyable {
  using ptr = std::shared_ptr<task>;

  task(const core::buffer buffer, async_send_completion_handler&& handler);

  core::buffer get();

  void operator()(const executor_type& executor, const boost::system::error_code& error_code);

  core::buffer m_buffer;

  async_send_completion_handler m_handler;
};

template <class transport, typename send_handler>
void async_send_func(transport& next_layer, const core::buffer buffer, BOOST_ASIO_MOVE_ARG(send_handler) handler);

template <class NextLayer, bool deflateSupported, typename send_handler>
void async_send_func(boost::beast::websocket::stream<NextLayer, deflateSupported>& next_layer, const core::buffer buffer, BOOST_ASIO_MOVE_ARG(send_handler) handler);

template <class transport>
template <typename arg_type>
queue<transport>::queue(arg_type&& arg, core::allocator::ptr allocator)
{
  m_impl = std::allocate_shared<impl>(core::allocator::wrapper<impl>(allocator), arg, allocator);
}

template <class transport>
template <typename arg_type>
queue<transport>::queue(arg_type& arg, core::allocator::ptr allocator)
{
  m_impl = std::allocate_shared<impl>(core::allocator::wrapper<impl>(allocator), arg, allocator);
}

template <class transport>
typename queue<transport>::next_layer_type& queue<transport>::next_layer()
{
  return m_impl->next_layer();
}

template <class transport>
const typename queue<transport>::next_layer_type& queue<transport>::next_layer() const
{
  return m_impl->next_layer();
}

template <class transport>
typename queue<transport>::executor_type queue<transport>::get_executor() const
{
  return m_impl->get_executor();
}

template <class transport>
void queue<transport>::cancel()
{
  m_impl->cancel();
}

template <class transport>
void queue<transport>::async_send_internal(const core::buffer buffer, async_send_completion_handler&& handler)
{
  m_impl->async_send(buffer, std::move(handler));
}

template <class transport>
template <typename arg_type>
queue<transport>::impl::impl(arg_type&& arg, core::allocator::ptr allocator)
    : m_next_layer(BOOST_ASIO_MOVE_CAST(arg_type)(arg)),
      m_strand(m_next_layer.get_executor()),
      m_allocator(allocator),
      m_queue(allocator)

{
}

template <class transport>
template <typename arg_type>
queue<transport>::impl::impl(arg_type& arg, core::allocator::ptr allocator)
    : m_next_layer(arg),
      m_strand(m_next_layer.get_executor()),
      m_allocator(allocator),
      m_queue(allocator)

{
}

template <class transport>
typename queue<transport>::next_layer_type& queue<transport>::impl::next_layer()
{
  return m_next_layer;
}

template <class transport>
const typename queue<transport>::next_layer_type& queue<transport>::impl::next_layer() const
{
  return m_next_layer;
}

template <class transport>
typename queue<transport>::executor_type queue<transport>::impl::get_executor() const
{
  return m_next_layer.get_executor();
}

template <class transport>
void queue<transport>::impl::async_send(const core::buffer buffer, async_send_completion_handler&& handler)
{
  boost::asio::dispatch(m_strand, std::bind_front(&queue<transport>::impl::send, std::enable_shared_from_this<impl>::shared_from_this(), std::allocate_shared<task>(core::allocator::wrapper<task>(m_allocator), buffer, std::move(handler))));
}

template <class transport>
void queue<transport>::impl::cancel()
{
  boost::asio::dispatch(m_strand, std::bind_front(&queue<transport>::impl::cancel_internal, std::enable_shared_from_this<impl>::shared_from_this()));
}

template <class transport>
void queue<transport>::impl::cancel_internal()
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  auto error_code = error::code::operation_cancelled;
  while (!m_queue.empty()) {
    auto task = m_queue.front();
    m_queue.pop();
    task->operator()(get_executor(), error_code);
  }
}

template <class transport>
void queue<transport>::impl::send(typename task::ptr task)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (!m_current) {
    process(task);
  }
  else {
    m_queue.push(task);
  }
}

template <class transport>
void queue<transport>::impl::process(typename task::ptr task)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  m_current               = task;
  auto completion_handler = std::bind(&queue<transport>::impl::on_send, std::enable_shared_from_this<impl>::shared_from_this(), std::placeholders::_1);
  async_send_func(m_next_layer, m_current->get(), boost::asio::bind_executor(boost::asio::any_io_executor{m_strand}, completion_handler));  // see : https://github.com/boostorg/beast/issues/2775
}

template <class transport>
void queue<transport>::impl::on_send(const boost::system::error_code& error_code)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  m_current->operator()(get_executor(), error_code);
  m_current = nullptr;
  if (!m_queue.empty()) {
    auto task = m_queue.front();
    m_queue.pop();
    process(task);
  }
}

template <class transport>
queue<transport>::impl::task::task(const core::buffer buffer, async_send_completion_handler&& handler)
    : m_buffer(buffer),
      m_handler(std::move(handler))
{
}

template <class transport>
core::buffer queue<transport>::impl::task::get()
{
  return m_buffer;
}

template <class transport>
void queue<transport>::impl::task::operator()(const executor_type& executor, const boost::system::error_code& error_code)
{
  rstream::core::invoke_completion_handler(executor, std::move(m_handler), error_code);
}

template <class transport, typename send_handler>
void async_send_func(transport& next_layer, const core::buffer buffer, BOOST_ASIO_MOVE_ARG(send_handler) handler)
{
  next_layer.async_send(buffer, std::move(handler));
}

template <class NextLayer, bool deflateSupported, typename send_handler>
void async_send_func(boost::beast::websocket::stream<NextLayer, deflateSupported>& next_layer, const core::buffer buffer, BOOST_ASIO_MOVE_ARG(send_handler) handler)
{
  next_layer.async_write(core::helpers::const_memory_sequence(buffer), std::move(handler));
}

}  // namespace io
}  // namespace rstream
