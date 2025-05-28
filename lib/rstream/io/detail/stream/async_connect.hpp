// See LICENSE file in the project root for license information.

#pragma once

#include <boost/asio/async_result.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/bind_executor.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/redirect_error.hpp>

#include <rstream/core/completion_handler.hpp>
#include <rstream/core/log.hpp>

#include "error.hpp"
#include "stream_socket.hpp"

namespace rstream {
namespace io {
namespace detail {
namespace stream {

}
}  // namespace detail
}  // namespace io
}  // namespace rstream

namespace boost {
namespace asio {

template <typename iterator, BOOST_ASIO_COMPLETION_TOKEN_FOR(void(const boost::system::error_code&, iterator)) iterator_connect_handler>
BOOST_ASIO_INITFN_AUTO_RESULT_TYPE(iterator_connect_handler, void(const boost::system::error_code&, iterator))
async_connect(rstream::io::detail::stream::stream_socket& socket, iterator begin, iterator end, BOOST_ASIO_MOVE_ARG(iterator_connect_handler) handler)
{
  return boost::asio::async_initiate<iterator_connect_handler, void(const boost::system::error_code&, iterator)>(
      [&socket](auto&& handler, iterator begin, iterator end) {
        auto awaitable = [](rstream::io::detail::stream::stream_socket& socket, iterator begin, iterator end) -> boost::asio::awaitable<iterator> {
          iterator it;
          rstream::core::logger logger({"rstream", "io", "stream", "connect"});
          boost::system::error_code error_code;
          if (begin == end) {
            error_code = boost::asio::error::not_found;
          }
          else {
            for (it = begin; it != end; ++it) {
#ifdef DEBUG_BUILD
              logger->trace("connecting to {}...", it->endpoint().to_string());
#endif
              co_await socket.async_connect(it->endpoint(), boost::asio::redirect_error(boost::asio::use_awaitable, error_code));
              if (!error_code) {
                break;
              }
              else {
                boost::system::error_code tmp;
                socket.close(tmp);
              }
            }
          }
          if (error_code) {
            throw boost::system::system_error(error_code);
          }
          co_return it;
        };
        boost::asio::co_spawn(
            socket.get_executor(), awaitable(socket, begin, end),
            [&socket, handler = std::move(handler)](const std::exception_ptr exception, iterator it) mutable {
              boost::system::error_code error_code;
              if (exception) {
                try {
                  std::rethrow_exception(exception);
                }
                catch (const boost::system::system_error& system_error) {
                  error_code = system_error.code();
                }
                catch (...) {
                  error_code = rstream::io::detail::stream::error::code::generic_error;
                }
              }
              rstream::core::invoke_completion_handler(socket.get_executor(), std::move(handler), error_code, it);
            });
      },
      handler, begin, end);
}

template <typename endpoint_sequence, BOOST_ASIO_COMPLETION_TOKEN_FOR(void(const boost::system::error_code&, rstream::io::detail::stream::endpoint)) range_connect_handler>
BOOST_ASIO_INITFN_AUTO_RESULT_TYPE(range_connect_handler, void(const boost::system::error_code&, rstream::io::detail::stream::endpoint))
async_connect(rstream::io::detail::stream::stream_socket& socket, const endpoint_sequence& endpoints, BOOST_ASIO_MOVE_ARG(range_connect_handler) handler)
{
  return boost::asio::async_initiate<range_connect_handler, void(const boost::system::error_code&, rstream::io::detail::stream::endpoint)>(
      [&socket](auto&& handler, const endpoint_sequence& endpoints) {
        auto sequence = std::make_shared<endpoint_sequence>(endpoints);
        async_connect(
            socket, sequence->cbegin(), sequence->cend(),
            [&socket, sequence, handler = std::move(handler)](const boost::system::error_code& error_code, typename endpoint_sequence::const_iterator it) mutable {
              rstream::core::invoke_completion_handler(socket.get_executor(), std::move(handler), error_code, error_code ? rstream::io::detail::stream::endpoint() : it->endpoint());
            });
      },
      handler, endpoints);
}

}  // namespace asio
}  // namespace boost
