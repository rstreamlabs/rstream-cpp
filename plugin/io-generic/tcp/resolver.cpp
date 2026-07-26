// See LICENSE file in the project root for license information.

#include "resolver.hpp"

#include <vector>

#include <boost/asio/dispatch.hpp>
#include <boost/asio/ip/address.hpp>
#include <boost/optional.hpp>

#include <rstream/core/completion_handler.hpp>
#include <rstream/io/detail/stream/url.hpp>

namespace rstream {
namespace plugin {
namespace io_generic {
namespace tcp {

}
}  // namespace io_generic
}  // namespace plugin
}  // namespace rstream

template <>
void rstream::plugin::io_generic::tcp::resolver::async_resolve_internal(const boost::urls::url& url, rstream::core::completion_handler<void(const boost::system::error_code&, const boost::asio::ip::tcp::resolver::results_type&)>&& handler)
{
  auto filter = [](const boost::asio::ip::tcp::resolver::results_type& results, const std::string& host, const std::string& port, bool inet4, bool inet6) {
    std::vector<boost::asio::ip::tcp::resolver::results_type::value_type> endpoints;
    for (const auto& entry : results) {
      if (inet4 && entry.endpoint().address().is_v6()) {
        continue;
      }
      if (inet6 && entry.endpoint().address().is_v4()) {
        continue;
      }
      endpoints.push_back(entry);
    }
    return boost::asio::ip::tcp::resolver::results_type::create(endpoints.begin(), endpoints.end(), host, port);
  };
  bool inet4      = false;
  bool inet6      = false;
  bool no_resolve = false;
  const std::string host(url.host());
  const std::string port(url.port());
  boost::system::error_code error_code;
  const auto params = rstream::io::detail::stream::url_params(url);
  if (!error_code) {
    auto it = params.find("tcp.inet4");
    if (it != params.end()) {
      rstream::io::detail::stream::parse_url_param_value(inet4, *it, error_code);
    }
  }
  if (!error_code) {
    auto it = params.find("tcp.inet6");
    if (it != params.end()) {
      rstream::io::detail::stream::parse_url_param_value(inet6, *it, error_code);
    }
  }
  if (!error_code) {
    auto it = params.find("tcp.no_resolve");
    if (it != params.end()) {
      rstream::io::detail::stream::parse_url_param_value(no_resolve, *it, error_code);
    }
  }
  if (inet4 && inet6) {
#ifdef DEBUG_BUILD
    m_logger->warn("both inet4 and inet6 options are set");
#endif
  }
  auto completion_handler = [filter, host, port, inet4, inet6, ex = io_object::get_executor(), handler = std::move(handler)](const boost::system::error_code& error_code, const boost::asio::ip::tcp::resolver::results_type& results) mutable {
    rstream::core::invoke_completion_handler(ex, std::move(handler), error_code, filter(results, host, port, inet4, inet6));
  };
  boost::optional<boost::asio::ip::tcp::endpoint> endpoint;
  if (no_resolve) {
#ifdef DEBUG_BUILD
    m_logger->trace("no-resolve option set to true");
#endif
    auto address = boost::asio::ip::make_address(host, error_code);
    if (!error_code) {
      endpoint = boost::asio::ip::tcp::endpoint(address, url.port_number());
    }
    else {
#ifdef DEBUG_BUILD
      m_logger->trace("failed to parse the IP address [host: {}, error_code: {}]", host, error_code.message());
#endif
    }
  }
  if (error_code) {
    completion_handler(error_code, boost::asio::ip::tcp::resolver::results_type());
  }
  else if (endpoint) {
    completion_handler(boost::system::error_code(), boost::asio::ip::tcp::resolver::results_type::create(endpoint.get(), host, port));
  }
  else {
    auto flags = boost::asio::ip::tcp::resolver::flags::all_matching;
    m_resolver.async_resolve(host, port, flags, std::move(completion_handler));
  }
}
