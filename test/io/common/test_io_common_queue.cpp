// See LICENSE file in the project root for license information.

#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <boost/asio/associated_cancellation_slot.hpp>
#include <boost/asio/associated_executor.hpp>
#include <boost/asio/async_result.hpp>
#include <boost/asio/bind_cancellation_slot.hpp>
#include <boost/asio/cancellation_signal.hpp>
#include <boost/asio/deferred.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/post.hpp>

#include <rstream/core/buffer.hpp>
#include <rstream/core/completion_handler.hpp>
#include <rstream/io/error.hpp>
#include <rstream/io/queue.hpp>

class controlled_transport {
 public:
  using executor_type      = boost::asio::io_context::executor_type;
  using completion_handler = rstream::core::completion_handler<void(const boost::system::error_code&)>;

  explicit controlled_transport(const executor_type& executor, bool auto_complete = false)
      : m_executor(executor),
        m_auto_complete(auto_complete)
  {
  }

  executor_type get_executor() const
  {
    return m_executor;
  }

  template <typename SendHandler>
  auto async_send(const rstream::core::buffer buffer, BOOST_ASIO_MOVE_ARG(SendHandler) handler)
  {
    return boost::asio::async_initiate<SendHandler, void(const boost::system::error_code&)>(
        [this](auto&& handler, const rstream::core::buffer buffer) {
          auto operation     = pending_operation::create(m_executor, completion_handler(std::forward<decltype(handler)>(handler)), [this] {
            std::lock_guard<std::mutex> lock(m_mutex);
            --m_active;
          });
          std::uint8_t value = 0;
          assert(buffer.extract(&value, 0, sizeof(value)) == sizeof(value));
          {
            std::lock_guard<std::mutex> lock(m_mutex);
            ++m_active;
            m_maximum_active = std::max(m_maximum_active, m_active);
            m_values.push_back(value);
            m_operations.push_back(operation);
          }
          if (m_auto_complete) {
            operation->complete(boost::system::error_code());
          }
        },
        handler, buffer);
  }

  void complete_next(const boost::system::error_code& error_code)
  {
    std::shared_ptr<pending_operation> operation;
    {
      std::lock_guard<std::mutex> lock(m_mutex);
      auto iterator = std::find_if(m_operations.begin(), m_operations.end(), [](const auto& candidate) { return !candidate->completed(); });
      assert(iterator != m_operations.end());
      operation = *iterator;
    }
    operation->complete(error_code);
  }

  std::size_t started() const
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_operations.size();
  }

  std::size_t maximum_active() const
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_maximum_active;
  }

  std::vector<std::uint8_t> values() const
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_values;
  }

 private:
  class pending_operation : public std::enable_shared_from_this<pending_operation> {
   public:
    static std::shared_ptr<pending_operation> create(const executor_type& executor, completion_handler&& handler, std::function<void()> on_complete)
    {
      auto operation = std::shared_ptr<pending_operation>(new pending_operation(executor, std::move(handler), std::move(on_complete)));
      operation->arm_cancellation();
      return operation;
    }

    bool completed() const
    {
      return m_completed.load();
    }

    void complete(const boost::system::error_code& error_code)
    {
      if (m_completed.exchange(true)) {
        return;
      }
      m_on_complete();
      auto executor = boost::asio::get_associated_executor(m_handler, m_executor);
      boost::asio::post(m_executor, [executor, handler = std::move(m_handler), error_code]() mutable {
        executor.execute([handler = std::move(handler), error_code]() mutable { std::move(handler)(error_code); });
      });
    }

   private:
    pending_operation(const executor_type& executor, completion_handler&& handler, std::function<void()> on_complete)
        : m_executor(executor),
          m_handler(std::move(handler)),
          m_on_complete(std::move(on_complete))
    {
    }

    void arm_cancellation()
    {
      auto slot = boost::asio::get_associated_cancellation_slot(m_handler);
      if (slot.is_connected()) {
        std::weak_ptr<pending_operation> weak = shared_from_this();
        slot.assign([weak, executor = m_executor](boost::asio::cancellation_type type) {
          if (type != boost::asio::cancellation_type::none) {
            boost::asio::post(executor, [weak] {
              if (auto operation = weak.lock()) {
                operation->complete(boost::asio::error::operation_aborted);
              }
            });
          }
        });
      }
    }

    executor_type m_executor;
    completion_handler m_handler;
    std::function<void()> m_on_complete;
    std::atomic_bool m_completed = false;
  };

  executor_type m_executor;
  bool m_auto_complete;
  mutable std::mutex m_mutex;
  std::size_t m_active         = 0;
  std::size_t m_maximum_active = 0;
  std::vector<std::uint8_t> m_values;
  std::vector<std::shared_ptr<pending_operation>> m_operations;
};

class move_only_transport {
 public:
  using executor_type = boost::asio::io_context::executor_type;

  explicit move_only_transport(const executor_type& executor)
      : m_executor(executor)
  {
  }

  move_only_transport(const move_only_transport&)            = delete;
  move_only_transport& operator=(const move_only_transport&) = delete;
  move_only_transport(move_only_transport&&)                 = default;
  move_only_transport& operator=(move_only_transport&&)      = default;

  executor_type get_executor() const
  {
    return m_executor;
  }

  template <typename SendHandler>
  auto async_send(const rstream::core::buffer buffer, BOOST_ASIO_MOVE_ARG(SendHandler) handler)
  {
    return boost::asio::async_initiate<SendHandler, void(const boost::system::error_code&)>(
        [executor = m_executor](auto&& handler, const rstream::core::buffer) {
          boost::asio::post(executor, [handler = std::forward<decltype(handler)>(handler)]() mutable {
            std::move(handler)(boost::system::error_code());
          });
        },
        handler, buffer);
  }

 private:
  executor_type m_executor;
};

static rstream::core::buffer make_buffer(std::uint8_t value)
{
  auto buffer = rstream::core::make_buffer_allocated(sizeof(value));
  assert(buffer.fill(&value, 0, sizeof(value)) == sizeof(value));
  return buffer;
}

static void check_owned_move_only_transport()
{
  boost::asio::io_context io_context;
  rstream::io::queue<move_only_transport> queue(move_only_transport(io_context.get_executor()));
  assert(queue.get_executor() == io_context.get_executor());
}

static void check_active_cancellation_reaches_transport()
{
  boost::asio::io_context io_context;
  controlled_transport transport(io_context.get_executor());
  rstream::io::queue<controlled_transport&> queue(transport);
  boost::asio::cancellation_signal cancellation;
  bool called = false;
  queue.async_send(
      make_buffer(1),
      boost::asio::bind_cancellation_slot(cancellation.slot(), [&](const boost::system::error_code& error_code) {
        assert(error_code == boost::asio::error::operation_aborted);
        called = true;
      }));
  io_context.poll();
  assert(transport.started() == 1);
  io_context.restart();
  cancellation.emit(boost::asio::cancellation_type::terminal);
  io_context.run();
  assert(called);
}

static void check_queued_cancellation_prevents_send()
{
  boost::asio::io_context io_context;
  controlled_transport transport(io_context.get_executor());
  rstream::io::queue<controlled_transport&> queue(transport);
  boost::asio::cancellation_signal cancellation;
  bool first_called  = false;
  bool second_called = false;
  queue.async_send(make_buffer(1), [&](const boost::system::error_code& error_code) {
    assert(!error_code);
    first_called = true;
  });
  queue.async_send(
      make_buffer(2),
      boost::asio::bind_cancellation_slot(cancellation.slot(), [&](const boost::system::error_code& error_code) {
        assert(error_code == rstream::io::error::code::operation_cancelled);
        second_called = true;
      }));
  io_context.poll();
  assert(transport.started() == 1);
  io_context.restart();
  cancellation.emit(boost::asio::cancellation_type::terminal);
  io_context.run();
  assert(second_called);
  assert(transport.started() == 1);
  transport.complete_next(boost::system::error_code());
  io_context.restart();
  io_context.run();
  assert(first_called);
  assert(transport.started() == 1);
}

static void check_concurrent_sends_are_serialized()
{
  constexpr std::size_t producer_count     = 4;
  constexpr std::size_t sends_per_producer = 32;
  constexpr std::size_t send_count         = producer_count * sends_per_producer;
  boost::asio::io_context io_context;
  controlled_transport transport(io_context.get_executor(), true);
  rstream::io::queue<controlled_transport&> queue(transport);
  std::atomic_size_t completed = 0;
  std::vector<std::thread> producers;
  producers.reserve(producer_count);
  for (std::size_t producer = 0; producer < producer_count; ++producer) {
    producers.emplace_back([producer, &queue, &completed] {
      for (std::size_t index = 0; index < sends_per_producer; ++index) {
        const auto value = static_cast<std::uint8_t>((producer * sends_per_producer) + index);
        queue.async_send(make_buffer(value), [&completed](const boost::system::error_code& error_code) {
          assert(!error_code);
          ++completed;
        });
      }
    });
  }
  for (auto& producer : producers) {
    producer.join();
  }
  std::vector<std::thread> workers;
  workers.reserve(4);
  for (std::size_t index = 0; index < 4; ++index) {
    workers.emplace_back([&io_context] { io_context.run(); });
  }
  for (auto& worker : workers) {
    worker.join();
  }
  auto values = transport.values();
  std::sort(values.begin(), values.end());
  assert(completed == send_count);
  assert(transport.started() == send_count);
  assert(transport.maximum_active() == 1);
  assert(values.size() == send_count);
  for (std::size_t index = 0; index < values.size(); ++index) {
    assert(values[index] == index);
  }
}

static void check_cancel_stops_active_and_queued_sends()
{
  boost::asio::io_context io_context;
  controlled_transport transport(io_context.get_executor());
  rstream::io::queue<controlled_transport&> queue(transport);
  std::size_t first_calls  = 0;
  std::size_t second_calls = 0;
  queue.async_send(make_buffer(1), [&](const boost::system::error_code& error_code) {
    assert(error_code == boost::asio::error::operation_aborted);
    ++first_calls;
  });
  queue.async_send(make_buffer(2), [&](const boost::system::error_code& error_code) {
    assert(error_code == rstream::io::error::code::operation_cancelled);
    ++second_calls;
  });
  io_context.poll();
  assert(transport.started() == 1);
  io_context.restart();
  queue.cancel();
  io_context.run();
  assert(first_calls == 1);
  assert(second_calls == 1);
  assert(transport.started() == 1);
}

static void check_cancel_precedes_subsequent_send()
{
  boost::asio::io_context io_context;
  controlled_transport transport(io_context.get_executor());
  rstream::io::queue<controlled_transport&> queue(transport);
  std::size_t cancelled_calls = 0;
  std::size_t completed_calls = 0;
  queue.async_send(make_buffer(1), [&](const boost::system::error_code& error_code) {
    assert(error_code == boost::asio::error::operation_aborted);
    ++cancelled_calls;
  });
  queue.cancel();
  queue.async_send(make_buffer(2), [&](const boost::system::error_code& error_code) {
    assert(!error_code);
    ++completed_calls;
  });
  io_context.poll();
  assert(cancelled_calls == 1);
  assert(completed_calls == 0);
  assert(transport.started() == 2);
  assert(transport.values() == std::vector<std::uint8_t>({1, 2}));
  transport.complete_next(boost::system::error_code());
  io_context.restart();
  io_context.run();
  assert(cancelled_calls == 1);
  assert(completed_calls == 1);
}

static void check_async_cancel_waits_for_active_send()
{
  boost::asio::io_context io_context;
  controlled_transport transport(io_context.get_executor());
  rstream::io::queue<controlled_transport&> queue(transport);
  std::vector<std::string> events;
  queue.async_send(make_buffer(1), [&](const boost::system::error_code& error_code) {
    assert(error_code == boost::asio::error::operation_aborted);
    events.emplace_back("active");
  });
  queue.async_send(make_buffer(2), [&](const boost::system::error_code& error_code) {
    assert(error_code == rstream::io::error::code::operation_cancelled);
    events.emplace_back("queued");
  });
  io_context.poll();
  assert(transport.started() == 1);
  io_context.restart();
  queue.async_cancel([&](const boost::system::error_code& error_code) {
    assert(!error_code);
    events.emplace_back("cancel");
  });
  assert(io_context.run_one() == 1);
  assert(events.empty());
  io_context.run();
  assert(events == std::vector<std::string>({"queued", "active", "cancel"}));
}

static void check_async_cancel_supports_multiple_waiters()
{
  boost::asio::io_context io_context;
  controlled_transport transport(io_context.get_executor());
  rstream::io::queue<controlled_transport&> queue(transport);
  std::size_t send_calls   = 0;
  std::size_t cancel_calls = 0;
  queue.async_send(make_buffer(1), [&](const boost::system::error_code& error_code) {
    assert(error_code == boost::asio::error::operation_aborted);
    ++send_calls;
  });
  io_context.poll();
  assert(transport.started() == 1);
  io_context.restart();
  for (std::size_t index = 0; index < 4; ++index) {
    queue.async_cancel([&](const boost::system::error_code& error_code) {
      assert(!error_code);
      ++cancel_calls;
    });
  }
  io_context.run();
  assert(send_calls == 1);
  assert(cancel_calls == 4);
}

static void check_deferred_operations_are_lazy()
{
  boost::asio::io_context io_context;
  controlled_transport transport(io_context.get_executor(), true);
  rstream::io::queue<controlled_transport&> queue(transport);
  auto send_operation   = queue.async_send(make_buffer(1), boost::asio::deferred);
  auto cancel_operation = queue.async_cancel(boost::asio::deferred);
  assert(transport.started() == 0);
  assert(io_context.poll() == 0);
  io_context.restart();

  std::size_t send_calls = 0;
  std::move(send_operation)([&](const boost::system::error_code& error_code) {
    assert(!error_code);
    ++send_calls;
  });
  io_context.run();
  assert(send_calls == 1);
  assert(transport.started() == 1);

  io_context.restart();
  std::size_t cancel_calls = 0;
  std::move(cancel_operation)([&](const boost::system::error_code& error_code) {
    assert(!error_code);
    ++cancel_calls;
  });
  assert(cancel_calls == 0);
  io_context.run();
  assert(cancel_calls == 1);
}

int main()
{
  check_owned_move_only_transport();
  check_queued_cancellation_prevents_send();
  check_active_cancellation_reaches_transport();
  check_concurrent_sends_are_serialized();
  check_cancel_stops_active_and_queued_sends();
  check_cancel_precedes_subsequent_send();
  check_async_cancel_waits_for_active_send();
  check_async_cancel_supports_multiple_waiters();
  check_deferred_operations_are_lazy();
  return 0;
}
