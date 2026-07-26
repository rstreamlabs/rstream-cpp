// See LICENSE file in the project root for license information.

#pragma once

#include <functional>
#include <tuple>
#include <type_traits>
#include <utility>

#include <boost/asio/any_completion_handler.hpp>
#include <boost/asio/associated_allocator.hpp>
#include <boost/asio/associated_cancellation_slot.hpp>
#include <boost/asio/associated_executor.hpp>
#include <boost/asio/associator.hpp>
#include <boost/asio/bind_allocator.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/post.hpp>

namespace rstream {
namespace core {

template <typename... Signatures>
using completion_handler = boost::asio::any_completion_handler<Signatures...>;

template <typename Fp>
struct wrap_function_helper;

template <typename Rp, typename... Args>
struct wrap_function_helper<Rp(Args...)> {
 public:
  template <typename Executor, typename Function>
  std::function<Rp(Args...)> operator()(const Executor& executor, Function function)
  {
    return [executor, function](Args... args) {
      boost::asio::dispatch(executor, std::bind(function, std::forward<Args>(args)...));
    };
  }
};

template <typename Fp, typename Executor, typename Function>
typename std::function<Fp> wrap_function(const Executor& executor, Function function)
{
  return wrap_function_helper<Fp>()(executor, function);
}

namespace detail {

template <typename Associations, typename Function>
class associated_handler {
 public:
  template <typename CompletionAssociations, typename CompletionFunction>
  associated_handler(CompletionAssociations&& associations, CompletionFunction&& function)
      : m_associations(std::forward<CompletionAssociations>(associations)),
        m_function(std::forward<CompletionFunction>(function))
  {
  }

  const Associations& associations() const noexcept
  {
    return m_associations;
  }

  template <typename... Args>
  decltype(auto) operator()(Args&&... args)
  {
    return std::invoke(m_function, m_associations, std::forward<Args>(args)...);
  }

 private:
  Associations m_associations;
  Function m_function;
};

template <typename Executor, typename Handler, typename... Args>
class completion_invocation {
 public:
  using executor_type          = boost::asio::associated_executor_t<Handler, Executor>;
  using allocator_type         = boost::asio::associated_allocator_t<Handler>;
  using cancellation_slot_type = boost::asio::associated_cancellation_slot_t<Handler>;

  template <typename CompletionHandler, typename... CompletionArgs>
  completion_invocation(const Executor& fallback_executor, CompletionHandler&& handler, CompletionArgs&&... args)
      : m_fallback_executor(fallback_executor),
        m_handler(std::forward<CompletionHandler>(handler)),
        m_args(std::forward<CompletionArgs>(args)...)
  {
  }

  executor_type get_executor() const noexcept
  {
    return boost::asio::get_associated_executor(m_handler, m_fallback_executor);
  }

  allocator_type get_allocator() const noexcept
  {
    return boost::asio::get_associated_allocator(m_handler);
  }

  cancellation_slot_type get_cancellation_slot() const noexcept
  {
    return boost::asio::get_associated_cancellation_slot(m_handler);
  }

  void operator()()
  {
    std::apply(
        [this](auto&&... args) {
          std::move(m_handler)(std::forward<decltype(args)>(args)...);
        },
        std::move(m_args));
  }

 private:
  Executor m_fallback_executor;
  Handler m_handler;
  std::tuple<Args...> m_args;
};

}  // namespace detail

template <typename Associations, typename Function>
auto bind_associated_handler(Associations&& associations, Function&& function)
{
  return detail::associated_handler<std::decay_t<Associations>, std::decay_t<Function>>(std::forward<Associations>(associations), std::forward<Function>(function));
}

template <typename Allocator, typename Function>
auto bind_handler_allocator(Allocator&& allocator, Function&& function)
{
  return bind_associated_handler(
      boost::asio::bind_allocator(std::forward<Allocator>(allocator), std::forward<Function>(function)),
      [](auto& associated_handler, auto&&... args) mutable -> decltype(auto) {
        return std::move(associated_handler)(std::forward<decltype(args)>(args)...);
      });
}

template <typename Owner, typename Handler>
auto bind_handler_lifetime(Owner&& owner, Handler&& handler)
{
  return bind_associated_handler(
      std::forward<Handler>(handler),
      [owner = std::forward<Owner>(owner)](auto& associated_handler, auto&&... args) mutable -> decltype(auto) {
        (void)owner;
        return std::move(associated_handler)(std::forward<decltype(args)>(args)...);
      });
}

template <typename Executor, typename Handler, typename... Args>
void invoke_completion_handler(const Executor& ctx, Handler&& handler, Args&&... args)
{
  using invocation_type = detail::completion_invocation<std::decay_t<Executor>, std::decay_t<Handler>, std::decay_t<Args>...>;
  boost::asio::post(ctx, invocation_type(ctx, std::forward<Handler>(handler), std::forward<Args>(args)...));
}

}  // namespace core
}  // namespace rstream

namespace boost {
namespace asio {

template <template <typename, typename> class Associator, typename Associations, typename Function, typename DefaultCandidate>
struct associator<Associator, rstream::core::detail::associated_handler<Associations, Function>, DefaultCandidate> : Associator<Associations, DefaultCandidate> {
  using handler_type = rstream::core::detail::associated_handler<Associations, Function>;

  static typename Associator<Associations, DefaultCandidate>::type get(const handler_type& handler) noexcept
  {
    return Associator<Associations, DefaultCandidate>::get(handler.associations());
  }

  static auto get(const handler_type& handler, const DefaultCandidate& candidate) noexcept
      -> decltype(Associator<Associations, DefaultCandidate>::get(handler.associations(), candidate))
  {
    return Associator<Associations, DefaultCandidate>::get(handler.associations(), candidate);
  }
};

}  // namespace asio
}  // namespace boost
