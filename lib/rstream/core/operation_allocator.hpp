// See LICENSE file in the project root for license information.

#pragma once

#include <cstddef>
#include <type_traits>
#include <utility>

#include <boost/asio/any_completion_handler.hpp>
#include <boost/asio/associated_allocator.hpp>

#include <rstream/core/allocator.hpp>

namespace rstream {
namespace core {
namespace detail {

template <typename Allocator>
struct is_erased_handler_allocator : std::false_type {};

template <typename T, typename... Signatures>
struct is_erased_handler_allocator<boost::asio::any_completion_handler_allocator<T, Signatures...>> : std::true_type {};

}  // namespace detail

// An erased handler allocator borrows the handler. A shared operation's control
// block may survive completion (for example through cancellation weak pointers).
// Keep that storage on the object's owning allocator; ordinary custom allocator
// associations and the handler's dispatch/completion associations are preserved.
template <typename Handler>
auto shared_operation_allocator(const Handler& handler, allocator::ptr fallback = {})
{
  auto associated = boost::asio::get_associated_allocator(handler);
  if constexpr (detail::is_erased_handler_allocator<decltype(associated)>::value) {
    return allocator::wrapper<std::byte>(std::move(fallback));
  }
  else {
    return associated;
  }
}

}  // namespace core
}  // namespace rstream
