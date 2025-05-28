// See LICENSE file in the project root for license information.

#pragma once

#include <list>

#include <boost/asio/async_result.hpp>
#include <boost/asio/ip/basic_resolver.hpp>
#include <boost/system/error_code.hpp>
#include <boost/url.hpp>

#include <rstream/core/completion_handler.hpp>
#include <rstream/core/log.hpp>

#include "io_object.hpp"

namespace rstream {
namespace io {

template <class T>
struct resolver_entry {
 public:
  resolver_entry(const T& endpoint, const boost::urls::url& url);

  T endpoint() const;

  boost::urls::url url() const;

 private:
  T m_endpoint;

  boost::urls::url m_url;
};

template <class T>
class resolver_results : public std::list<resolver_entry<T>> {
 public:
  using endpoint_type = T;
};

template <class T>
class resolver_base : public io_object {
 public:
  using results_type = resolver_results<T>;

  resolver_base(const executor_type& executor);

  virtual ~resolver_base() = default;

  using async_resolve_completion_handler = rstream::core::completion_handler<void(const boost::system::error_code&, const results_type&)>;
  template <typename resolve_handler>
  BOOST_ASIO_INITFN_RESULT_TYPE(BOOST_ASIO_MOVE_ARG(resolve_handler), void(const boost::system::error_code&, const results_type&))
  async_resolve(const boost::urls::url& url, BOOST_ASIO_MOVE_ARG(resolve_handler) handler)
  {
    return boost::asio::async_initiate<resolve_handler, void(const boost::system::error_code&, const results_type&)>([this](auto&& handler, const boost::urls::url& url) { async_resolve_internal(url, std::forward<decltype(handler)>(handler)); }, handler, url);
  }

  template <typename resolve_handler>
  BOOST_ASIO_INITFN_RESULT_TYPE(BOOST_ASIO_MOVE_ARG(resolve_handler), void(const boost::system::error_code&, const results_type&))
  async_resolve(const std::string& uri, BOOST_ASIO_MOVE_ARG(resolve_handler) handler)
  {
    auto url = boost::urls::parse_uri(uri);
    if (url.has_error()) {
#ifdef DEBUG_BUILD
      rstream::core::default_logger()->warn("URL is not valid [uri: {}, error_code: {}]", uri, url.error().message());
#endif
      return boost::asio::async_initiate<resolve_handler, void(const boost::system::error_code&, const results_type&)>(
          [this, error_code = url.error()](auto&& handler) {
            rstream::core::invoke_completion_handler(get_executor(), std::move(handler), error_code, results_type());
          },
          handler);
    }
    else {
      return async_resolve(url.value(), std::forward<decltype(handler)>(handler));
    }
  }

  virtual void cancel() = 0;

 private:
  virtual void async_resolve_internal(const boost::urls::url& url, async_resolve_completion_handler&& handler) = 0;
};

template <class T>
resolver_entry<T>::resolver_entry(const T& endpoint, const boost::urls::url& url)
    : m_endpoint(endpoint),
      m_url(url)
{
}

template <class T>
T resolver_entry<T>::endpoint() const
{
  return m_endpoint;
}

template <class T>
boost::urls::url resolver_entry<T>::url() const
{
  return m_url;
}

template <class T>
resolver_base<T>::resolver_base(const executor_type& executor)
    : io_object(executor)
{
}

}  // namespace io
}  // namespace rstream
