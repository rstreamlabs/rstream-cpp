// See LICENSE file in the project root for license information.

#include <atomic>
#include <cassert>
#include <condition_variable>
#include <cstddef>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/bind_executor.hpp>
#include <boost/asio/cancellation_signal.hpp>
#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/strand.hpp>
#include <boost/system/error_code.hpp>

#include <rstream/core/completion_handler.hpp>

struct allocation_state {
  std::atomic_size_t m_allocations   = 0;
  std::atomic_size_t m_deallocations = 0;
};

struct lifetime_state {
  explicit lifetime_state(bool& destroyed)
      : m_destroyed(destroyed)
  {
  }

  ~lifetime_state()
  {
    m_destroyed = true;
  }

  bool& m_destroyed;
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

 private:
  std::shared_ptr<allocation_state> m_state;
};

class associated_handler {
 public:
  using executor_type          = boost::asio::strand<boost::asio::io_context::executor_type>;
  using allocator_type         = counting_allocator<std::byte>;
  using cancellation_slot_type = boost::asio::cancellation_slot;

  associated_handler(executor_type executor, allocator_type allocator, cancellation_slot_type cancellation_slot, std::atomic_size_t& calls)
      : m_executor(std::move(executor)),
        m_allocator(std::move(allocator)),
        m_cancellation_slot(cancellation_slot),
        m_calls(calls),
        m_move_only_state(std::make_unique<int>(42))
  {
  }

  associated_handler(associated_handler&&) noexcept = default;

  associated_handler& operator=(associated_handler&&) noexcept = delete;

  associated_handler(const associated_handler&) = delete;

  associated_handler& operator=(const associated_handler&) = delete;

  executor_type get_executor() const noexcept
  {
    return m_executor;
  }

  allocator_type get_allocator() const noexcept
  {
    return m_allocator;
  }

  cancellation_slot_type get_cancellation_slot() const noexcept
  {
    return m_cancellation_slot;
  }

  void operator()(const boost::system::error_code& error_code)
  {
    assert(!error_code);
    assert(m_executor.running_in_this_thread());
    assert(m_move_only_state && *m_move_only_state == 42);
    ++m_calls;
  }

 private:
  executor_type m_executor;
  allocator_type m_allocator;
  cancellation_slot_type m_cancellation_slot;
  std::atomic_size_t& m_calls;
  std::unique_ptr<int> m_move_only_state;
};

static void check_type_erasure_preserves_associations()
{
  boost::asio::io_context associated_context;
  auto strand     = boost::asio::make_strand(associated_context);
  auto allocation = std::make_shared<allocation_state>();
  boost::asio::cancellation_signal cancellation;
  std::atomic_size_t calls = 0;
  rstream::core::completion_handler<void(const boost::system::error_code&)> handler(
      associated_handler(strand, counting_allocator<std::byte>(allocation), cancellation.slot(), calls));
  assert(boost::asio::get_associated_cancellation_slot(handler).is_connected());
  bool cancellation_observed = false;
  boost::asio::get_associated_cancellation_slot(handler).assign([&cancellation_observed](boost::asio::cancellation_type type) {
    assert(type == boost::asio::cancellation_type::total);
    cancellation_observed = true;
  });
  cancellation.emit(boost::asio::cancellation_type::total);
  assert(cancellation_observed);
  assert(allocation->m_allocations > 0);
}

static void check_completion_is_deferred_on_associated_executor()
{
  boost::asio::io_context fallback_context;
  boost::asio::io_context associated_context;
  auto strand     = boost::asio::make_strand(associated_context);
  auto allocation = std::make_shared<allocation_state>();
  boost::asio::cancellation_signal cancellation;
  std::atomic_size_t calls = 0;
  rstream::core::completion_handler<void(const boost::system::error_code&)> handler(
      associated_handler(strand, counting_allocator<std::byte>(allocation), cancellation.slot(), calls));
  const auto allocations_before_completion = allocation->m_allocations.load();
  rstream::core::invoke_completion_handler(fallback_context.get_executor(), std::move(handler), boost::system::error_code());
  assert(calls == 0);
  assert(allocation->m_allocations > allocations_before_completion);
  fallback_context.poll();
  assert(calls == 0);
  associated_context.run();
  assert(calls == 1);
  associated_context.restart();
  associated_context.poll();
  assert(calls == 1);
  assert(allocation->m_allocations == allocation->m_deallocations);
}

static void check_completion_can_dispatch_on_associated_executor()
{
  boost::asio::io_context fallback_context;
  boost::asio::io_context associated_context;
  auto strand     = boost::asio::make_strand(associated_context);
  auto allocation = std::make_shared<allocation_state>();
  boost::asio::cancellation_signal cancellation;
  std::atomic_size_t calls = 0;
  auto work = boost::asio::make_work_guard(associated_context);
  boost::asio::post(strand, [&] {
    rstream::core::completion_handler<void(const boost::system::error_code&)> handler(
        associated_handler(strand, counting_allocator<std::byte>(allocation), cancellation.slot(), calls));
    rstream::core::dispatch_completion_handler(fallback_context.get_executor(), std::move(handler), boost::system::error_code());
    assert(calls == 1);
    work.reset();
  });
  associated_context.run();
  assert(calls == 1);
  assert(allocation->m_allocations == allocation->m_deallocations);
}

static void check_abandoned_completion_releases_associated_state()
{
  auto allocation = std::make_shared<allocation_state>();
  std::atomic_size_t calls = 0;
  {
    boost::asio::io_context associated_context;
    boost::asio::io_context fallback_context;
    auto strand = boost::asio::make_strand(associated_context);
    boost::asio::cancellation_signal cancellation;
    rstream::core::completion_handler<void(const boost::system::error_code&)> handler(
        associated_handler(strand, counting_allocator<std::byte>(allocation), cancellation.slot(), calls));
    rstream::core::invoke_completion_handler(fallback_context.get_executor(), std::move(handler), boost::system::error_code());
  }
  assert(calls == 0);
  assert(allocation->m_allocations > 0);
  assert(allocation->m_allocations == allocation->m_deallocations);
}

static void check_adapter_preserves_associations()
{
  boost::asio::io_context fallback_context;
  boost::asio::io_context associated_context;
  auto strand     = boost::asio::make_strand(associated_context);
  auto allocation = std::make_shared<allocation_state>();
  boost::asio::cancellation_signal cancellation;
  std::atomic_size_t calls = 0;
  auto handler             = rstream::core::bind_associated_handler(
      associated_handler(strand, counting_allocator<std::byte>(allocation), cancellation.slot(), calls),
      [](auto& handler, const boost::system::error_code& error_code) { std::move(handler)(error_code); });
  assert(boost::asio::get_associated_executor(handler) == strand);
  assert(boost::asio::get_associated_allocator(handler).state() == allocation);
  assert(boost::asio::get_associated_cancellation_slot(handler).is_connected());
  bool cancellation_observed = false;
  boost::asio::get_associated_cancellation_slot(handler).assign([&cancellation_observed](boost::asio::cancellation_type type) {
    assert(type == boost::asio::cancellation_type::total);
    cancellation_observed = true;
  });
  cancellation.emit(boost::asio::cancellation_type::total);
  assert(cancellation_observed);
  rstream::core::invoke_completion_handler(fallback_context.get_executor(), std::move(handler), boost::system::error_code());
  fallback_context.poll();
  assert(calls == 0);
  associated_context.run();
  assert(calls == 1);
}

static void check_lifetime_adapter_preserves_owner_and_associations()
{
  boost::asio::io_context fallback_context;
  boost::asio::io_context associated_context;
  auto strand     = boost::asio::make_strand(associated_context);
  auto allocation = std::make_shared<allocation_state>();
  boost::asio::cancellation_signal cancellation;
  std::atomic_size_t calls                 = 0;
  bool owner_destroyed                     = false;
  auto owner                               = std::make_shared<lifetime_state>(owner_destroyed);
  std::weak_ptr<lifetime_state> weak_owner = owner;
  auto handler                             = rstream::core::bind_handler_lifetime(
      owner,
      associated_handler(strand, counting_allocator<std::byte>(allocation), cancellation.slot(), calls));
  owner.reset();
  assert(!weak_owner.expired());
  assert(boost::asio::get_associated_executor(handler) == strand);
  assert(boost::asio::get_associated_allocator(handler).state() == allocation);
  assert(boost::asio::get_associated_cancellation_slot(handler).is_connected());
  rstream::core::invoke_completion_handler(
      fallback_context.get_executor(), std::move(handler),
      boost::system::error_code());
  fallback_context.poll();
  assert(calls == 0);
  assert(!weak_owner.expired());
  associated_context.run();
  assert(calls == 1);
  assert(weak_owner.expired());
  assert(owner_destroyed);
}

static void check_strand_serializes_type_erased_handlers()
{
  boost::asio::io_context io_context;
  auto strand = boost::asio::make_strand(io_context);
  auto work   = boost::asio::make_work_guard(io_context);
  std::mutex mutex;
  std::condition_variable condition;
  bool first_entered       = false;
  bool release_first       = false;
  bool second_entered      = false;
  bool sentinel_entered    = false;
  std::atomic_size_t calls = 0;
  rstream::core::completion_handler<void(const boost::system::error_code&)> first_handler(
      boost::asio::bind_executor(strand, [&](const boost::system::error_code& error_code) {
        assert(!error_code);
        {
          std::lock_guard lock(mutex);
          first_entered = true;
        }
        condition.notify_all();
        std::unique_lock lock(mutex);
        condition.wait(lock, [&release_first] {
          return release_first;
        });
        ++calls;
      }));
  rstream::core::invoke_completion_handler(
      io_context.get_executor(), std::move(first_handler),
      boost::system::error_code());
  std::thread first_thread([&io_context] {
    io_context.run();
  });
  std::thread second_thread([&io_context] {
    io_context.run();
  });
  {
    std::unique_lock lock(mutex);
    condition.wait(lock, [&first_entered] {
      return first_entered;
    });
  }
  rstream::core::completion_handler<void(const boost::system::error_code&)> second_handler(
      boost::asio::bind_executor(strand, [&](const boost::system::error_code& error_code) {
        assert(!error_code);
        {
          std::lock_guard lock(mutex);
          second_entered = true;
        }
        condition.notify_all();
        ++calls;
      }));
  rstream::core::invoke_completion_handler(
      io_context.get_executor(), std::move(second_handler),
      boost::system::error_code());
  boost::asio::post(io_context, [&] {
    {
      std::lock_guard lock(mutex);
      sentinel_entered = true;
    }
    condition.notify_all();
  });
  {
    std::unique_lock lock(mutex);
    condition.wait(lock, [&sentinel_entered] {
      return sentinel_entered;
    });
    assert(!second_entered);
    release_first = true;
  }
  condition.notify_all();
  work.reset();
  first_thread.join();
  second_thread.join();
  assert(second_entered);
  assert(calls == 2);
}

int main()
{
  check_type_erasure_preserves_associations();
  check_completion_is_deferred_on_associated_executor();
  check_completion_can_dispatch_on_associated_executor();
  check_abandoned_completion_releases_associated_state();
  check_adapter_preserves_associations();
  check_lifetime_adapter_preserves_owner_and_associations();
  check_strand_serializes_type_erased_handlers();
  return 0;
}
