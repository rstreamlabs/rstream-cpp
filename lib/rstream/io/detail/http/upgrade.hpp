// See LICENSE file in the project root for license information.

#pragma once

#include <functional>
#include <memory>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>

#include <boost/asio/async_result.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>

#include <rstream/core/allocator.hpp>
#include <rstream/core/completion_handler.hpp>
#include <rstream/core/log.hpp>
#include <rstream/io/error.hpp>

namespace rstream {
namespace io {
namespace detail {
namespace http {

template <class allocator>
bool is_upgrade_request(const boost::beast::http::header<true, boost::beast::http::basic_fields<allocator>>& request);

template <class allocator>
bool is_upgrade_response(const boost::beast::http::header<false, boost::beast::http::basic_fields<allocator>>& response);

template <class stream>
class upgrade {
 public:
  using next_layer_type = typename std::remove_reference<stream>::type;

  using request_type = boost::beast::http::request<boost::beast::http::empty_body>;

  using response_type = boost::beast::http::response<boost::beast::http::empty_body>;

  using request_decorator = std::function<void(request_type&)>;

  using response_decorator = std::function<void(response_type&)>;

  template <typename arg_type>
  upgrade(arg_type&& arg, core::allocator::ptr allocator = nullptr);

  template <typename arg_type>
  upgrade(arg_type& arg, core::allocator::ptr allocator = nullptr);

  next_layer_type& next_layer();

  const next_layer_type& next_layer() const;

  template <typename Decorator>
  void set_decorator(Decorator&& decorator);

  template <typename async_handshake_completion_handler>
  auto async_handshake(const std::string& host, const std::string& target, BOOST_ASIO_MOVE_ARG(async_handshake_completion_handler) handler);

  template <typename async_accept_completion_handler>
  auto async_accept(BOOST_ASIO_MOVE_ARG(async_accept_completion_handler) handler);

 private:
  template <typename T>
  class async_handshake_operation;

  template <typename T>
  class async_accept_operation;

  stream m_next_layer;

  core::allocator::ptr m_allocator;

  request_decorator m_request_decorator;

  response_decorator m_response_decorator;
};

template <class stream>
template <typename T>
class upgrade<stream>::async_handshake_operation : public std::enable_shared_from_this<async_handshake_operation<T>> {
 public:
  using handler_type = typename std::remove_reference<T>::type;

  async_handshake_operation(stream& next_layer, core::allocator::ptr allocator, const std::string& host, const std::string& target, const request_decorator& decorator);

  void run(handler_type handler);

  void do_write_request(handler_type handler);

  void on_write_request(handler_type handler, const boost::system::error_code& error_code, std::size_t length);

  void do_read_response(handler_type handler);

  void on_read_response(handler_type handler, const boost::system::error_code& error_code, std::size_t length);

  void on_error(handler_type handler, const boost::system::error_code& error_code);

  void on_complete(handler_type handler);

 private:
  stream& m_next_layer;

  rstream::core::logger m_logger;

  const std::string m_host;

  const std::string m_target;

  request_decorator m_decorator;

  boost::beast::basic_flat_buffer<core::allocator::wrapper<char>> m_buffer;

  boost::beast::http::request<boost::beast::http::empty_body> m_request;

  boost::beast::http::response<boost::beast::http::empty_body> m_response;
};

template <class stream>
template <typename T>
class upgrade<stream>::async_accept_operation : public std::enable_shared_from_this<async_accept_operation<T>> {
 public:
  using handler_type = typename std::remove_reference<T>::type;

  async_accept_operation(stream& next_layer, core::allocator::ptr allocator, const response_decorator& decorator);

  void run(handler_type handler);

  void do_read_request(handler_type handler);

  void on_read_request(handler_type handler, const boost::system::error_code& error_code, std::size_t length);

  void do_write_response(handler_type handler);

  void on_write_response(handler_type handler, const boost::system::error_code& error_code, std::size_t length);

  void on_error(handler_type handler, const boost::system::error_code& error_code);

  void on_complete(handler_type handler);

 private:
  stream& m_next_layer;

  rstream::core::logger m_logger;

  response_decorator m_decorator;

  boost::beast::basic_flat_buffer<core::allocator::wrapper<char>> m_buffer;

  boost::beast::http::request<boost::beast::http::empty_body> m_request;

  boost::beast::http::response<boost::beast::http::empty_body> m_response;
};

template <class stream>
template <typename arg_type>
upgrade<stream>::upgrade(arg_type&& arg, core::allocator::ptr allocator)
    : m_next_layer(BOOST_ASIO_MOVE_CAST(arg_type)(arg)),
      m_allocator(allocator)
{
}

template <class stream>
template <typename arg_type>
upgrade<stream>::upgrade(arg_type& arg, core::allocator::ptr allocator)
    : m_next_layer(arg),
      m_allocator(allocator)
{
}

template <class stream>
typename upgrade<stream>::next_layer_type& upgrade<stream>::next_layer()
{
  return m_next_layer;
}

template <class stream>
const typename upgrade<stream>::next_layer_type& upgrade<stream>::next_layer() const
{
  return m_next_layer;
}

template <class stream>
template <typename Decorator>
void upgrade<stream>::set_decorator(Decorator&& decorator)
{
  using decorator_type              = std::decay_t<Decorator>;
  constexpr auto decorates_request  = std::is_invocable_v<decorator_type&, request_type&>;
  constexpr auto decorates_response = std::is_invocable_v<decorator_type&, response_type&>;
  static_assert(decorates_request || decorates_response, "HTTP upgrade decorators must accept a request or response");
  auto state           = std::make_shared<decorator_type>(std::forward<Decorator>(decorator));
  m_request_decorator  = {};
  m_response_decorator = {};
  if constexpr (decorates_request) {
    m_request_decorator = [state](request_type& request) { std::invoke(*state, request); };
  }
  if constexpr (decorates_response) {
    m_response_decorator = [state](response_type& response) { std::invoke(*state, response); };
  }
}

template <class stream>
template <typename handshake_handler>
auto upgrade<stream>::async_handshake(const std::string& host, const std::string& target, BOOST_ASIO_MOVE_ARG(handshake_handler) handler)
{
  return boost::asio::async_initiate<handshake_handler, void(const boost::system::error_code&)>(
      [this](auto&& handler, const std::string& host, const std::string& target) {
        using operation_type     = async_handshake_operation<std::decay_t<decltype(handler)>>;
        auto operation_allocator = boost::asio::get_associated_allocator(handler);
        std::allocate_shared<operation_type>(
            operation_allocator,
            m_next_layer,
            m_allocator,
            host,
            target,
            m_request_decorator)
            ->run(std::forward<decltype(handler)>(handler));
      },
      handler, host, target);
}

template <class stream>
template <typename accept_handler>
auto upgrade<stream>::async_accept(BOOST_ASIO_MOVE_ARG(accept_handler) handler)
{
  return boost::asio::async_initiate<accept_handler, void(const boost::system::error_code&)>(
      [this](auto&& handler) {
        using operation_type     = async_accept_operation<std::decay_t<decltype(handler)>>;
        auto operation_allocator = boost::asio::get_associated_allocator(handler);
        std::allocate_shared<operation_type>(
            operation_allocator,
            m_next_layer,
            m_allocator,
            m_response_decorator)
            ->run(std::forward<decltype(handler)>(handler));
      },
      handler);
}

template <class stream>
template <typename T>
upgrade<stream>::async_handshake_operation<T>::async_handshake_operation(stream& next_layer, core::allocator::ptr allocator, const std::string& host, const std::string& target, const request_decorator& decorator)
    : m_next_layer(next_layer),
      m_logger({"rstream", "io", "http", "upgrade", "handshake", fmt::format("#{}", fmt::ptr(this))}),
      m_host(host),
      m_target(target),
      m_decorator(decorator),
      m_buffer(allocator)
{
}

template <class stream>
template <typename T>
void upgrade<stream>::async_handshake_operation<T>::run(handler_type handler)
{
  do_write_request(std::move(handler));
}

template <class stream>
template <typename T>
void upgrade<stream>::async_handshake_operation<T>::do_write_request(handler_type handler)
{
  m_request = {};
  // prepare request
  m_request.method(boost::beast::http::verb::get);
  m_request.version(11);
  m_request.target(m_target);
  m_request.set(boost::beast::http::field::host, m_host);
  m_request.insert(boost::beast::http::field::upgrade, "rstrm");
  m_request.insert(boost::beast::http::field::connection, "upgrade");
  m_request.set(boost::beast::http::field::user_agent, BOOST_BEAST_VERSION_STRING);
  if (m_decorator) {
    try {
      m_decorator(m_request);
    }
    catch (const boost::system::system_error& error) {
      on_error(std::move(handler), error.code());
      return;
    }
    catch (...) {
      on_error(std::move(handler), error::code::unknown_undefined_error);
      return;
    }
  }
#ifdef DEBUG_BUILD
  {
    std::stringstream str;
    str << m_request.base();
    str.str(str.str().substr(0, str.str().find_last_not_of("\r\n") + 1));
    m_logger->trace("sending HTTP request :\n{}", str.str());
  }
#endif
  // send the request
  auto ptr                = async_handshake_operation::shared_from_this();
  auto completion_handler = rstream::core::bind_associated_handler(
      std::move(handler),
      [ptr](auto& handler, const boost::system::error_code& error_code, std::size_t length) mutable {
        ptr->on_write_request(std::move(handler), error_code, length);
      });
  boost::beast::http::async_write(m_next_layer, m_request, std::move(completion_handler));
}

template <class stream>
template <typename T>
void upgrade<stream>::async_handshake_operation<T>::on_write_request(handler_type handler, const boost::system::error_code& error_code, std::size_t length)
{
  (void)length;
  if (error_code) {
    on_error(std::move(handler), error_code);
  }
  else {
    do_read_response(std::move(handler));
  }
}

template <class stream>
template <typename T>
void upgrade<stream>::async_handshake_operation<T>::do_read_response(handler_type handler)
{
  m_response              = {};
  auto ptr                = async_handshake_operation::shared_from_this();
  auto completion_handler = rstream::core::bind_associated_handler(
      std::move(handler),
      [ptr](auto& handler, const boost::system::error_code& error_code, std::size_t length) mutable {
        ptr->on_read_response(std::move(handler), error_code, length);
      });
  boost::beast::http::async_read(m_next_layer, m_buffer, m_response, std::move(completion_handler));
}

template <class stream>
template <typename T>
void upgrade<stream>::async_handshake_operation<T>::on_read_response(handler_type handler, const boost::system::error_code& error_code, std::size_t length)
{
  (void)length;
  if (error_code) {
    on_error(std::move(handler), error_code);
  }
  else {
#ifdef DEBUG_BUILD
    {
      std::stringstream str;
      str << m_response.base();
      str.str(str.str().substr(0, str.str().find_last_not_of("\r\n") + 1));
      m_logger->trace("received HTTP response :\n{}", str.str());
    }
#endif
    if (is_upgrade_response(m_response)) {
      on_complete(std::move(handler));
    }
    else {
      on_error(std::move(handler), boost::beast::http::error::bad_status);
    }
  }
}

template <class stream>
template <typename T>
void upgrade<stream>::async_handshake_operation<T>::on_error(handler_type handler, const boost::system::error_code& error_code)
{
  rstream::core::invoke_completion_handler(m_next_layer.get_executor(), std::move(handler), error_code);
}

template <class stream>
template <typename T>
void upgrade<stream>::async_handshake_operation<T>::on_complete(handler_type handler)
{
  rstream::core::invoke_completion_handler(m_next_layer.get_executor(), std::move(handler), boost::system::error_code());
}

template <class stream>
template <typename T>
upgrade<stream>::async_accept_operation<T>::async_accept_operation(stream& next_layer, core::allocator::ptr allocator, const response_decorator& decorator)
    : m_next_layer(next_layer),
      m_logger({"rstream", "io", "http", "upgrade", "accept", fmt::format("#{}", fmt::ptr(this))}),
      m_decorator(decorator),
      m_buffer(allocator)
{
}

template <class stream>
template <typename T>
void upgrade<stream>::async_accept_operation<T>::run(handler_type handler)
{
  do_read_request(std::move(handler));
}

template <class stream>
template <typename T>
void upgrade<stream>::async_accept_operation<T>::do_read_request(handler_type handler)
{
  m_request               = {};
  auto ptr                = async_accept_operation::shared_from_this();
  auto completion_handler = rstream::core::bind_associated_handler(
      std::move(handler),
      [ptr](auto& handler, const boost::system::error_code& error_code, std::size_t length) mutable {
        ptr->on_read_request(std::move(handler), error_code, length);
      });
  boost::beast::http::async_read(m_next_layer, m_buffer, m_request, std::move(completion_handler));
}

template <class stream>
template <typename T>
void upgrade<stream>::async_accept_operation<T>::on_read_request(handler_type handler, const boost::system::error_code& error_code, std::size_t length)
{
  (void)length;
  if (error_code) {
    on_error(std::move(handler), error_code);
  }
  else {
#ifdef DEBUG_BUILD
    {
      std::stringstream str;
      str << m_request.base();
      str.str(str.str().substr(0, str.str().find_last_not_of("\r\n") + 1));
      m_logger->trace("received HTTP request :\n{}", str.str());
    }
#endif
    do_write_response(std::move(handler));
  }
}

template <class stream>
template <typename T>
void upgrade<stream>::async_accept_operation<T>::do_write_response(handler_type handler)
{
  m_response = {};
  // prepare response
  m_response.version(m_request.version());
  if (is_upgrade_request(m_request)) {
    m_response.result(boost::beast::http::status::switching_protocols);
    m_response.insert(boost::beast::http::field::upgrade, "rstrm");
    m_response.insert(boost::beast::http::field::connection, "upgrade");
  }
  else {
    m_response.result(boost::beast::http::status::bad_gateway);
  }
  if (m_decorator) {
    try {
      m_decorator(m_response);
    }
    catch (const boost::system::system_error& error) {
      on_error(std::move(handler), error.code());
      return;
    }
    catch (...) {
      on_error(std::move(handler), error::code::unknown_undefined_error);
      return;
    }
  }
#ifdef DEBUG_BUILD
  {
    std::stringstream str;
    str << m_response.base();
    str.str(str.str().substr(0, str.str().find_last_not_of("\r\n") + 1));
    m_logger->trace("sending HTTP response :\n{}", str.str());
  }
#endif
  // send the response
  auto ptr                = async_accept_operation::shared_from_this();
  auto completion_handler = rstream::core::bind_associated_handler(
      std::move(handler),
      [ptr](auto& handler, const boost::system::error_code& error_code, std::size_t length) mutable {
        ptr->on_write_response(std::move(handler), error_code, length);
      });
  boost::beast::http::async_write(m_next_layer, m_response, std::move(completion_handler));
}

template <class stream>
template <typename T>
void upgrade<stream>::async_accept_operation<T>::on_write_response(handler_type handler, const boost::system::error_code& error_code, std::size_t length)
{
  (void)length;
  if (error_code) {
    on_error(std::move(handler), error_code);
  }
  else {
    if (m_response.result() == boost::beast::http::status::switching_protocols) {
      on_complete(std::move(handler));
    }
    else {
      on_error(std::move(handler), boost::beast::http::error::bad_status);
    }
  }
}

template <class stream>
template <typename T>
void upgrade<stream>::async_accept_operation<T>::on_error(handler_type handler, const boost::system::error_code& error_code)
{
  rstream::core::invoke_completion_handler(m_next_layer.get_executor(), std::move(handler), error_code);
}

template <class stream>
template <typename T>
void upgrade<stream>::async_accept_operation<T>::on_complete(handler_type handler)
{
  rstream::core::invoke_completion_handler(m_next_layer.get_executor(), std::move(handler), boost::system::error_code());
}

template <class allocator>
bool is_upgrade_request(const boost::beast::http::header<true, boost::beast::http::basic_fields<allocator>>& request)
{
  if (request.version() < 11) {
    return false;
  }
  if (request.method() != boost::beast::http::verb::get) {
    return false;
  }
  if (!boost::beast::http::token_list{request[boost::beast::http::field::connection]}.exists("upgrade")) {
    return false;
  }
  if (!boost::beast::http::token_list{request[boost::beast::http::field::upgrade]}.exists("rstrm")) {
    return false;
  }
  return true;
}

template <class allocator>
bool is_upgrade_response(const boost::beast::http::header<false, boost::beast::http::basic_fields<allocator>>& response)
{
  if (response.version() < 11) {
    return false;
  }
  if (response.result() != boost::beast::http::status::switching_protocols) {
    return false;
  }
  if (!boost::beast::http::token_list{response[boost::beast::http::field::connection]}.exists("upgrade")) {
    return false;
  }
  if (!boost::beast::http::token_list{response[boost::beast::http::field::upgrade]}.exists("rstrm")) {
    return false;
  }
  return true;
}

}  // namespace http
}  // namespace detail
}  // namespace io
}  // namespace rstream
