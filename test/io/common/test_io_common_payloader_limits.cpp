// See LICENSE file in the project root for license information.

#include <atomic>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>

#include <boost/asio/bind_cancellation_slot.hpp>
#include <boost/asio/cancellation_signal.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/deferred.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/local/connect_pair.hpp>
#include <boost/asio/local/stream_protocol.hpp>
#include <boost/asio/socket_base.hpp>
#include <boost/asio/steady_timer.hpp>

#include <rstream/core/buffer.hpp>
#include <rstream/core/error.hpp>
#include <rstream/io/error.hpp>
#include <rstream/io/payloader.hpp>

struct allocation_state {
  std::atomic_size_t m_allocations   = 0;
  std::atomic_size_t m_deallocations = 0;
};

template <typename T>
class counting_allocator {
 public:
  using value_type = T;

  explicit counting_allocator(std::shared_ptr<allocation_state> state)
      : m_state(std::move(state))
  {
  }

  template <typename U>
  counting_allocator(const counting_allocator<U>& other)
      : m_state(other.state())
  {
  }

  T* allocate(std::size_t count)
  {
    ++m_state->m_allocations;
    return std::allocator<T>().allocate(count);
  }

  void deallocate(T* pointer, std::size_t count)
  {
    ++m_state->m_deallocations;
    std::allocator<T>().deallocate(pointer, count);
  }

  const std::shared_ptr<allocation_state>& state() const
  {
    return m_state;
  }

  template <typename U>
  bool operator==(const counting_allocator<U>& other) const
  {
    return m_state == other.state();
  }

  template <typename U>
  bool operator!=(const counting_allocator<U>& other) const
  {
    return !(*this == other);
  }

 private:
  template <typename>
  friend class counting_allocator;

  std::shared_ptr<allocation_state> m_state;
};

class associated_allocator_handler {
 public:
  using allocator_type = counting_allocator<std::byte>;

  associated_allocator_handler(allocator_type allocator, bool& called)
      : m_allocator(std::move(allocator)),
        m_called(called)
  {
  }

  allocator_type get_allocator() const
  {
    return m_allocator;
  }

  void operator()(const boost::system::error_code& error_code)
  {
    m_called = true;
    assert(error_code == boost::asio::error::operation_aborted);
  }

 private:
  allocator_type m_allocator;

  bool& m_called;
};

class allocation_observing_stream {
 public:
  using executor_type = boost::asio::io_context::executor_type;

  allocation_observing_stream(const executor_type& executor, std::shared_ptr<allocation_state> allocation)
      : m_executor(executor),
        m_allocation(std::move(allocation))
  {
  }

  executor_type get_executor() const
  {
    return m_executor;
  }

  template <typename mutable_buffer_sequence, typename read_handler>
  void async_read_some(const mutable_buffer_sequence&, read_handler&& handler)
  {
    assert(m_allocation->m_allocations > 0);
    boost::asio::post(
        m_executor,
        [handler = std::forward<read_handler>(handler)]() mutable {
          std::move(handler)(boost::asio::error::operation_aborted, 0);
        });
  }

 private:
  executor_type m_executor;

  std::shared_ptr<allocation_state> m_allocation;
};

class recording_stream {
 public:
  using executor_type = boost::asio::io_context::executor_type;

  explicit recording_stream(const executor_type& executor)
      : m_executor(executor)
  {
  }

  executor_type get_executor() const
  {
    return m_executor;
  }

  template <typename const_buffer_sequence, typename write_handler>
  void async_write_some(const const_buffer_sequence&, write_handler&& handler)
  {
    ++m_write_calls;
    boost::asio::post(
        m_executor,
        [handler = std::forward<write_handler>(handler)]() mutable {
          std::move(handler)(boost::asio::error::operation_not_supported, 0);
        });
  }

  std::size_t write_calls() const
  {
    return m_write_calls;
  }

 private:
  executor_type m_executor;

  std::size_t m_write_calls = 0;
};

static void check_oversized_payload_is_rejected_before_write()
{
  if (std::numeric_limits<std::size_t>::max() <= std::numeric_limits<std::uint32_t>::max()) {
    return;
  }
  boost::asio::io_context io_context;
  recording_stream stream(io_context.get_executor());
  rstream::io::payloader<recording_stream&> sender(stream);
  const std::uint8_t storage = 0;
  const auto size            = static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()) + 1;
  const auto memory          = rstream::core::make_memory_wrapped(&storage, size);
  const rstream::core::buffer send_buffer(memory);
  bool sender_called = false;
  sender.async_send(send_buffer, [&](const boost::system::error_code& error_code) {
    sender_called = true;
    assert(error_code == rstream::io::error::code::invalid_buffer_size);
  });
  assert(!sender_called);
  assert(stream.write_calls() == 0);
  io_context.run();
  assert(sender_called);
  assert(stream.write_calls() == 0);
}

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
  auto send_buffer     = rstream::core::make_buffer_allocated(16);
  auto recv_buffer     = rstream::core::make_buffer_allocated(4);
  bool sender_called   = false;
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

static void check_lvalue_handlers_are_supported()
{
  boost::asio::io_context io_context;
  using socket_type    = boost::asio::local::stream_protocol::socket;
  using payloader_type = rstream::io::payloader<socket_type&>;
  socket_type socket_a(io_context.get_executor());
  socket_type socket_b(io_context.get_executor());
  boost::asio::local::connect_pair(socket_a, socket_b);
  payloader_type sender(socket_a);
  payloader_type receiver(socket_b);
  auto send_buffer   = rstream::core::make_buffer_allocated(16);
  auto recv_buffer   = rstream::core::make_buffer_allocated(16);
  bool sender_called = false;
  auto send_handler  = [&](const boost::system::error_code& error_code) {
    sender_called = true;
    assert(!error_code);
  };
  bool receiver_called = false;
  auto recv_handler    = [&](const boost::system::error_code& error_code) {
    receiver_called = true;
    assert(!error_code);
  };
  sender.async_send(send_buffer, send_handler);
  receiver.async_recv(recv_buffer, recv_handler);
  io_context.run();
  assert(sender_called);
  assert(receiver_called);
}

static void check_deferred_send_and_receive_are_lazy()
{
  boost::asio::io_context io_context;
  using socket_type    = boost::asio::local::stream_protocol::socket;
  using payloader_type = rstream::io::payloader<socket_type&>;
  socket_type socket_a(io_context.get_executor());
  socket_type socket_b(io_context.get_executor());
  boost::asio::local::connect_pair(socket_a, socket_b);
  payloader_type sender(socket_a);
  payloader_type receiver(socket_b);
  auto send_buffer    = rstream::core::make_buffer_allocated(16);
  auto recv_buffer    = rstream::core::make_buffer_allocated(16);
  auto send_operation = sender.async_send(send_buffer, boost::asio::deferred);
  auto recv_operation = receiver.async_recv(recv_buffer, boost::asio::deferred);
  assert(io_context.poll() == 0);
  io_context.restart();

  std::size_t completion_count = 0;
  std::move(send_operation)([&](const boost::system::error_code& error_code) {
    assert(!error_code);
    ++completion_count;
  });
  std::move(recv_operation)([&](const boost::system::error_code& error_code) {
    assert(!error_code);
    ++completion_count;
  });
  assert(completion_count == 0);
  io_context.run();
  assert(completion_count == 2);
  assert(recv_buffer.get_size() == send_buffer.get_size());
}

static void check_receive_cancellation_reaches_the_transport()
{
  boost::asio::io_context io_context;
  using socket_type    = boost::asio::local::stream_protocol::socket;
  using payloader_type = rstream::io::payloader<socket_type&>;
  auto socket_a        = std::make_shared<socket_type>(io_context.get_executor());
  auto socket_b        = std::make_shared<socket_type>(io_context.get_executor());
  boost::asio::local::connect_pair(*socket_a, *socket_b);
  payloader_type receiver(*socket_b);
  auto recv_buffer = rstream::core::make_buffer_allocated(4);
  boost::asio::cancellation_signal cancellation;
  boost::asio::steady_timer deadline(io_context, std::chrono::milliseconds(200));
  bool deadline_expired = false;
  bool receiver_called  = false;
  deadline.async_wait([&](const boost::system::error_code& error_code) {
    if (!error_code) {
      deadline_expired = true;
      socket_b->close();
    }
  });
  receiver.async_recv(
      recv_buffer,
      boost::asio::bind_cancellation_slot(cancellation.slot(), [&](const boost::system::error_code& error_code) {
        receiver_called = true;
        assert(!deadline_expired);
        assert(error_code == boost::asio::error::operation_aborted);
        deadline.cancel();
      }));
  cancellation.emit(boost::asio::cancellation_type::terminal);
  io_context.run();
  assert(receiver_called);
  assert(!deadline_expired);
}

static void check_send_cancellation_reaches_the_transport()
{
  boost::asio::io_context io_context;
  using socket_type    = boost::asio::local::stream_protocol::socket;
  using payloader_type = rstream::io::payloader<socket_type&>;
  auto socket_a        = std::make_shared<socket_type>(io_context.get_executor());
  auto socket_b        = std::make_shared<socket_type>(io_context.get_executor());
  boost::asio::local::connect_pair(*socket_a, *socket_b);
  socket_a->set_option(boost::asio::socket_base::send_buffer_size(1024));
  payloader_type sender(*socket_a);
  auto send_buffer = rstream::core::make_buffer_allocated(1024 * 1024);
  boost::asio::cancellation_signal cancellation;
  boost::asio::steady_timer deadline(io_context, std::chrono::milliseconds(200));
  bool deadline_expired = false;
  bool sender_called    = false;
  deadline.async_wait([&](const boost::system::error_code& error_code) {
    if (!error_code) {
      deadline_expired = true;
      socket_a->close();
    }
  });
  sender.async_send(
      send_buffer,
      boost::asio::bind_cancellation_slot(cancellation.slot(), [&](const boost::system::error_code& error_code) {
        sender_called = true;
        assert(!deadline_expired);
        assert(error_code == boost::asio::error::operation_aborted);
        deadline.cancel();
      }));
  cancellation.emit(boost::asio::cancellation_type::terminal);
  io_context.run();
  assert(sender_called);
  assert(!deadline_expired);
}

static void check_control_callback_system_error_completes_receive()
{
  boost::asio::io_context io_context;
  using socket_type    = boost::asio::local::stream_protocol::socket;
  using payloader_type = rstream::io::payloader<socket_type&>;
  socket_type socket_a(io_context.get_executor());
  socket_type socket_b(io_context.get_executor());
  boost::asio::local::connect_pair(socket_a, socket_b);
  payloader_type sender(socket_a);
  payloader_type receiver(socket_b);
  receiver.set_control_callback(
      [](rstream::core::buffer&, std::size_t) {
        throw boost::system::system_error(rstream::core::error::code::invalid_size);
      });
  const auto send_buffer = rstream::core::make_buffer_allocated(16);
  const auto recv_buffer = rstream::core::make_buffer_allocated(16);
  bool receiver_called   = false;
  sender.async_send(send_buffer, [](const boost::system::error_code& error_code) {
    assert(!error_code);
  });
  receiver.async_recv(recv_buffer, [&](const boost::system::error_code& error_code) {
    receiver_called = true;
    assert(error_code == rstream::core::error::code::invalid_size);
  });
  io_context.run();
  assert(receiver_called);
}

static void check_control_callback_unknown_exception_completes_receive()
{
  boost::asio::io_context io_context;
  using socket_type    = boost::asio::local::stream_protocol::socket;
  using payloader_type = rstream::io::payloader<socket_type&>;
  socket_type socket_a(io_context.get_executor());
  socket_type socket_b(io_context.get_executor());
  boost::asio::local::connect_pair(socket_a, socket_b);
  payloader_type sender(socket_a);
  payloader_type receiver(socket_b);
  receiver.set_control_callback(
      [](rstream::core::buffer&, std::size_t) {
        throw 42;
      });
  const auto send_buffer = rstream::core::make_buffer_allocated(16);
  const auto recv_buffer = rstream::core::make_buffer_allocated(16);
  bool receiver_called   = false;
  sender.async_send(send_buffer, [](const boost::system::error_code& error_code) {
    assert(!error_code);
  });
  receiver.async_recv(recv_buffer, [&](const boost::system::error_code& error_code) {
    receiver_called = true;
    assert(error_code == rstream::io::error::code::unknown_undefined_error);
  });
  io_context.run();
  assert(receiver_called);
}

static void check_receive_operation_uses_associated_allocator()
{
  boost::asio::io_context io_context;
  auto allocation = std::make_shared<allocation_state>();
  allocation_observing_stream stream(io_context.get_executor(), allocation);
  rstream::io::payloader<allocation_observing_stream&> receiver(stream);
  auto recv_buffer = rstream::core::make_buffer_allocated(16);
  bool called      = false;
  receiver.async_recv(
      recv_buffer,
      associated_allocator_handler(
          counting_allocator<std::byte>(allocation),
          called));
  assert(allocation->m_allocations > 0);
  assert(!called);
  io_context.run();
  assert(called);
  assert(allocation->m_allocations == allocation->m_deallocations);
}

int main(int argc, char** argv)
{
  (void)argc;
  (void)argv;
  check_oversized_payload_is_rejected_before_write();
  check_payload_larger_than_buffer_is_rejected();
  check_lvalue_handlers_are_supported();
  check_deferred_send_and_receive_are_lazy();
  check_receive_cancellation_reaches_the_transport();
  check_send_cancellation_reaches_the_transport();
  check_control_callback_system_error_completes_receive();
  check_control_callback_unknown_exception_completes_receive();
  check_receive_operation_uses_associated_allocator();
  return 0;
}
