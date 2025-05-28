// See LICENSE file in the project root for license information.

#pragma once

#include <boost/asio/any_completion_handler.hpp>
#include <boost/asio/bind_executor.hpp>
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

template <typename Executor, typename Handler, typename... Args>
void invoke_completion_handler(const Executor& ctx, Handler&& handler, Args&&... args)
{
  auto executor = boost::asio::get_associated_executor(handler, ctx);
  boost::asio::post(ctx, boost::asio::bind_executor(executor, std::move(std::bind_front(std::forward<Handler>(handler), std::forward<Args>(args)...))));
}

}  // namespace core
}  // namespace rstream
