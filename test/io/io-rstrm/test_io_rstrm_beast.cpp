// See LICENSE file in the project root for license information.

#include <cassert>
#include <cstddef>
#include <memory>

#include <boost/asio/bind_allocator.hpp>
#include <boost/asio/bind_cancellation_slot.hpp>
#include <boost/asio/bind_executor.hpp>
#include <boost/asio/cancellation_signal.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/strand.hpp>

#include <rstream/io-rstrm/beast.hpp>

struct allocation_counts {
  std::size_t allocations   = 0;
  std::size_t deallocations = 0;
};

template <typename T>
struct counted_allocator {
  using value_type = T;
  std::shared_ptr<allocation_counts> counts;

  explicit counted_allocator(std::shared_ptr<allocation_counts> value)
      : counts(std::move(value))
  {
  }
  template <typename U>
  counted_allocator(const counted_allocator<U>& value)
      : counts(value.counts)
  {
  }
  T* allocate(std::size_t count)
  {
    ++counts->allocations;
    return std::allocator<T>().allocate(count);
  }
  void deallocate(T* value, std::size_t count)
  {
    ++counts->deallocations;
    std::allocator<T>().deallocate(value, count);
  }
  template <typename U>
  bool operator==(const counted_allocator<U>& value) const
  {
    return counts == value.counts;
  }
};

static void check_teardown_associations(bool erased)
{
  boost::asio::io_context transport;
  boost::asio::io_context completion;
  auto strand = boost::asio::make_strand(completion);
  auto counts = std::make_shared<allocation_counts>();
  boost::asio::cancellation_signal cancellation;
  std::size_t calls         = 0;
  std::size_t cancellations = 0;
  auto observe_cancellation = [&](boost::asio::cancellation_type type) {
    assert(type == boost::asio::cancellation_type::total);
    ++cancellations;
  };
  {
    rstream::io_rstrm::socket socket(transport.get_executor());
    auto completion_handler = boost::asio::bind_executor(strand,
                                                         [owned = std::make_unique<int>(42), &calls, &strand](const boost::system::error_code& error) {
                                                           assert(!error);
                                                           assert(strand.running_in_this_thread());
                                                           assert(owned && *owned == 42);
                                                           ++calls;
                                                         });
    auto allocated_handler  = boost::asio::bind_allocator(counted_allocator<std::byte>(counts), std::move(completion_handler));
    auto handler            = boost::asio::bind_cancellation_slot(cancellation.slot(), std::move(allocated_handler));
    assert(boost::asio::get_associated_cancellation_slot(handler) == cancellation.slot());
    // ADL customization, move-only handler, no captured reference to socket.
    if (erased) {
      rstream::core::completion_handler<void(const boost::system::error_code&)> erased_handler(std::move(handler));
      // Type erasure uses a proxy slot: test forwarding, not slot identity.
      auto slot = boost::asio::get_associated_cancellation_slot(erased_handler);
      assert(slot.is_connected());
      slot.assign(observe_cancellation);
      async_teardown(boost::beast::role_type::server, socket, std::move(erased_handler));
    }
    else {
      boost::asio::get_associated_cancellation_slot(handler).assign(observe_cancellation);
      async_teardown(boost::beast::role_type::server, socket, std::move(handler));
    }
    assert(calls == 0);
    cancellation.emit(boost::asio::cancellation_type::total);
    assert(cancellations == 1);
  }
  transport.run();
  assert(calls == 0);  // Completion belongs to the associated executor.
  completion.run();
  assert(calls == 1);
  cancellation.emit(boost::asio::cancellation_type::total);  // Late cancellation is harmless.
  assert(counts->allocations > 0 && counts->allocations == counts->deallocations);
}

int main()
{
  check_teardown_associations(false);
  check_teardown_associations(true);
}
