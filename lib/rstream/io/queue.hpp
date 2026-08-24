// See LICENSE file in the project root for license information.

#pragma once

#include <atomic>
#include <deque>
#include <memory>
#include <queue>

#include <boost/asio/associated_allocator.hpp>
#include <boost/asio/associated_cancellation_slot.hpp>
#include <boost/asio/associated_executor.hpp>
#include <boost/asio/async_result.hpp>
#include <boost/asio/bind_allocator.hpp>
#include <boost/asio/bind_cancellation_slot.hpp>
#include <boost/asio/bind_executor.hpp>
#include <boost/asio/cancellation_signal.hpp>
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

  virtual ~queue_base() = default;

  using async_send_completion_handler   = rstream::core::completion_handler<void(const boost::system::error_code&)>;
  using async_cancel_completion_handler = rstream::core::completion_handler<void(const boost::system::error_code&)>;
  template <typename send_handler>
  auto async_send(const core::buffer buffer, BOOST_ASIO_MOVE_ARG(send_handler) handler)
  {
    return boost::asio::async_initiate<send_handler, void(const boost::system::error_code&)>(
        [this](auto&& handler, const core::buffer buffer) {
          async_send_internal(buffer, std::forward<decltype(handler)>(handler));
        },
        handler, buffer);
  }

  template <typename cancel_handler>
  auto async_cancel(BOOST_ASIO_MOVE_ARG(cancel_handler) handler)
  {
    return boost::asio::async_initiate<cancel_handler, void(const boost::system::error_code&)>(
        [this](auto&& handler) {
          async_cancel_internal(std::forward<decltype(handler)>(handler));
        },
        handler);
  }

  virtual void cancel() = 0;

 private:
  virtual void async_send_internal(const core::buffer buffer, async_send_completion_handler&& handler) = 0;

  virtual void async_cancel_internal(async_cancel_completion_handler&& handler) = 0;
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

  void async_cancel_internal(async_cancel_completion_handler&& handler) override;

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

  void async_cancel(async_cancel_completion_handler&& handler);

  void async_send(const core::buffer buffer, async_send_completion_handler&& handler);

 private:
  struct task;

  void cancel_internal();

  void cancel_internal(async_cancel_completion_handler&& handler);

  void cancel_pending();

  void complete_cancel_handlers();

  void send(typename task::ptr task);

  void process(typename task::ptr task);

  void process_next();

  void on_task_cancel(const typename task::ptr& task, boost::asio::cancellation_type type);

  void on_send(async_send_completion_handler handler, const boost::system::error_code& error_code);

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

  /// completion handlers waiting for the active operation to stop
  std::deque<async_cancel_completion_handler, core::allocator::wrapper<async_cancel_completion_handler>> m_cancel_handlers;
};

template <class transport>
struct queue<transport>::impl::task : boost::noncopyable {
  using ptr = std::shared_ptr<task>;

  task(const core::buffer buffer, async_send_completion_handler&& handler);

  void arm(const ptr& self, const std::weak_ptr<impl>& owner, const boost::asio::strand<executor_type>& strand);

  bool cancelled() const;

  bool completed() const;

  boost::asio::cancellation_slot cancellation_slot();

  async_send_completion_handler& handler();

  core::buffer get();

  void request_cancel(boost::asio::cancellation_type type);

  void emit_cancellation(boost::asio::cancellation_type type);

  void complete(const executor_type& executor, const boost::system::error_code& error_code);

  void complete(const executor_type& executor, async_send_completion_handler handler, const boost::system::error_code& error_code);

  core::buffer m_buffer;

  async_send_completion_handler m_handler;

  boost::asio::cancellation_signal m_cancellation_signal;

  std::atomic<unsigned int> m_cancelled = 0;

  std::atomic_bool m_completed = false;
};

template <class transport, typename send_handler>
void async_send_func(transport& next_layer, const core::buffer buffer, BOOST_ASIO_MOVE_ARG(send_handler) handler);

template <class NextLayer, bool deflateSupported, typename send_handler>
void async_send_func(boost::beast::websocket::stream<NextLayer, deflateSupported>& next_layer, const core::buffer buffer, BOOST_ASIO_MOVE_ARG(send_handler) handler);

template <class transport>
template <typename arg_type>
queue<transport>::queue(arg_type&& arg, core::allocator::ptr allocator)
{
  m_impl = std::allocate_shared<impl>(
      core::allocator::wrapper<impl>(allocator),
      BOOST_ASIO_MOVE_CAST(arg_type)(arg),
      allocator);
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
void queue<transport>::async_cancel_internal(async_cancel_completion_handler&& handler)
{
  m_impl->async_cancel(std::move(handler));
}

template <class transport>
template <typename arg_type>
queue<transport>::impl::impl(arg_type&& arg, core::allocator::ptr allocator)
    : m_next_layer(BOOST_ASIO_MOVE_CAST(arg_type)(arg)),
      m_strand(m_next_layer.get_executor()),
      m_allocator(allocator),
      m_queue(allocator),
      m_cancel_handlers(allocator)
{
}

template <class transport>
template <typename arg_type>
queue<transport>::impl::impl(arg_type& arg, core::allocator::ptr allocator)
    : m_next_layer(arg),
      m_strand(m_next_layer.get_executor()),
      m_allocator(allocator),
      m_queue(allocator),
      m_cancel_handlers(allocator)
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
  auto self     = std::enable_shared_from_this<impl>::shared_from_this();
  auto task_ptr = std::allocate_shared<task>(core::allocator::wrapper<task>(m_allocator), buffer, std::move(handler));
  task_ptr->arm(task_ptr, self, m_strand);
  boost::asio::dispatch(m_strand, boost::asio::bind_allocator(core::allocator::wrapper<char>(m_allocator), [self, task_ptr] { self->send(task_ptr); }));
}

template <class transport>
void queue<transport>::impl::cancel()
{
  auto self = std::enable_shared_from_this<impl>::shared_from_this();
  boost::asio::dispatch(m_strand, [self] { self->cancel_internal(); });
}

template <class transport>
void queue<transport>::impl::async_cancel(async_cancel_completion_handler&& handler)
{
  auto self = std::enable_shared_from_this<impl>::shared_from_this();
  boost::asio::dispatch(
      m_strand,
      boost::asio::bind_allocator(
          core::allocator::wrapper<char>(m_allocator),
          [self, handler = std::move(handler)]() mutable {
            self->cancel_internal(std::move(handler));
          }));
}

template <class transport>
void queue<transport>::impl::cancel_internal()
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  cancel_pending();
}

template <class transport>
void queue<transport>::impl::cancel_internal(async_cancel_completion_handler&& handler)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  m_cancel_handlers.push_back(std::move(handler));
  cancel_pending();
  complete_cancel_handlers();
}

template <class transport>
void queue<transport>::impl::cancel_pending()
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  auto error_code = error::code::operation_cancelled;
  if (m_current) {
    m_current->request_cancel(boost::asio::cancellation_type::terminal);
    m_current->emit_cancellation(boost::asio::cancellation_type::terminal);
  }
  while (!m_queue.empty()) {
    auto task = m_queue.front();
    m_queue.pop();
    task->request_cancel(boost::asio::cancellation_type::terminal);
    task->complete(get_executor(), error_code);
  }
}

template <class transport>
void queue<transport>::impl::complete_cancel_handlers()
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (m_current) {
    return;
  }
  while (!m_cancel_handlers.empty()) {
    auto handler = std::move(m_cancel_handlers.front());
    m_cancel_handlers.pop_front();
    rstream::core::invoke_completion_handler(get_executor(), std::move(handler), boost::system::error_code());
  }
}

template <class transport>
void queue<transport>::impl::send(typename task::ptr task)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (task->completed()) {
    return;
  }
  if (task->cancelled()) {
    task->complete(get_executor(), error::code::operation_cancelled);
    return;
  }
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
  if (task->completed()) {
    process_next();
    return;
  }
  if (task->cancelled()) {
    task->complete(get_executor(), error::code::operation_cancelled);
    process_next();
    return;
  }
  m_current               = task;
  auto self               = std::enable_shared_from_this<impl>::shared_from_this();
  auto completion_handler = rstream::core::bind_associated_handler(
      std::move(m_current->handler()),
      [self](auto& handler, const boost::system::error_code& error_code, auto&&...) { self->on_send(std::move(handler), error_code); });
  async_send_func(
      m_next_layer, m_current->get(),
      boost::asio::bind_cancellation_slot(
          m_current->cancellation_slot(),
          boost::asio::bind_executor(boost::asio::any_io_executor{m_strand}, std::move(completion_handler))));  // see : https://github.com/boostorg/beast/issues/2775
}

template <class transport>
void queue<transport>::impl::process_next()
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  while (!m_queue.empty()) {
    auto task = m_queue.front();
    m_queue.pop();
    if (task->completed()) {
      continue;
    }
    if (task->cancelled()) {
      task->complete(get_executor(), error::code::operation_cancelled);
      continue;
    }
    process(task);
    return;
  }
}

template <class transport>
void queue<transport>::impl::on_task_cancel(const typename task::ptr& task, boost::asio::cancellation_type type)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (task->completed()) {
    return;
  }
  if (task == m_current) {
    task->emit_cancellation(type);
  }
  else {
    task->complete(get_executor(), error::code::operation_cancelled);
  }
}

template <class transport>
void queue<transport>::impl::on_send(async_send_completion_handler handler, const boost::system::error_code& error_code)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  m_current->complete(get_executor(), std::move(handler), error_code);
  m_current = nullptr;
  complete_cancel_handlers();
  process_next();
}

template <class transport>
queue<transport>::impl::task::task(const core::buffer buffer, async_send_completion_handler&& handler)
    : m_buffer(buffer),
      m_handler(std::move(handler))
{
}

template <class transport>
void queue<transport>::impl::task::arm(const ptr& self, const std::weak_ptr<impl>& owner, const boost::asio::strand<executor_type>& strand)
{
  auto slot = boost::asio::get_associated_cancellation_slot(m_handler);
  if (slot.is_connected()) {
    std::weak_ptr<task> weak = self;
    slot.assign([weak, owner, strand](boost::asio::cancellation_type type) {
      if (type == boost::asio::cancellation_type::none) {
        return;
      }
      auto task_ptr = weak.lock();
      if (!task_ptr) {
        return;
      }
      task_ptr->request_cancel(type);
      boost::asio::dispatch(strand, [weak, owner, type] {
        auto task_ptr  = weak.lock();
        auto owner_ptr = owner.lock();
        if (task_ptr && owner_ptr) {
          owner_ptr->on_task_cancel(task_ptr, type);
        }
      });
    });
  }
}

template <class transport>
bool queue<transport>::impl::task::cancelled() const
{
  return m_cancelled.load() != 0;
}

template <class transport>
bool queue<transport>::impl::task::completed() const
{
  return m_completed.load();
}

template <class transport>
boost::asio::cancellation_slot queue<transport>::impl::task::cancellation_slot()
{
  return m_cancellation_signal.slot();
}

template <class transport>
typename queue<transport>::async_send_completion_handler& queue<transport>::impl::task::handler()
{
  return m_handler;
}

template <class transport>
core::buffer queue<transport>::impl::task::get()
{
  return m_buffer;
}

template <class transport>
void queue<transport>::impl::task::request_cancel(boost::asio::cancellation_type type)
{
  m_cancelled.fetch_or(static_cast<unsigned int>(type));
}

template <class transport>
void queue<transport>::impl::task::emit_cancellation(boost::asio::cancellation_type type)
{
  m_cancellation_signal.emit(type);
}

template <class transport>
void queue<transport>::impl::task::complete(const executor_type& executor, const boost::system::error_code& error_code)
{
  if (m_completed.exchange(true)) {
    return;
  }
  rstream::core::invoke_completion_handler(executor, std::move(m_handler), error_code);
}

template <class transport>
void queue<transport>::impl::task::complete(const executor_type& executor, async_send_completion_handler handler, const boost::system::error_code& error_code)
{
  if (m_completed.exchange(true)) {
    return;
  }
  rstream::core::invoke_completion_handler(executor, std::move(handler), error_code);
}

template <class transport, typename send_handler>
void async_send_func(transport& next_layer, const core::buffer buffer, BOOST_ASIO_MOVE_ARG(send_handler) handler)
{
  next_layer.async_send(buffer, BOOST_ASIO_MOVE_CAST(send_handler)(handler));
}

template <class NextLayer, bool deflateSupported, typename send_handler>
void async_send_func(boost::beast::websocket::stream<NextLayer, deflateSupported>& next_layer, const core::buffer buffer, BOOST_ASIO_MOVE_ARG(send_handler) handler)
{
  next_layer.async_write(
      core::helpers::const_memory_sequence(buffer),
      BOOST_ASIO_MOVE_CAST(send_handler)(handler));
}

}  // namespace io
}  // namespace rstream
