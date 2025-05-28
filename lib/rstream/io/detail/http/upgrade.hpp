// See LICENSE file in the project root for license information.

#pragma once

#include <sstream>
#include <string>

#include <boost/asio/async_result.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>
#include <boost/beast/websocket/detail/decorator.hpp>
#include <boost/beast/websocket/stream_base.hpp>
#include <boost/optional.hpp>

#include <rstream/core/allocator.hpp>
#include <rstream/core/completion_handler.hpp>
#include <rstream/core/log.hpp>

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

  template <typename arg_type>
  upgrade(arg_type&& arg, core::allocator::ptr allocator = nullptr);

  template <typename arg_type>
  upgrade(arg_type& arg, core::allocator::ptr allocator = nullptr);

  next_layer_type& next_layer();

  const next_layer_type& next_layer() const;

  void set_decorator(boost::beast::websocket::stream_base::decorator decorator);

  template <typename async_handshake_completion_handler>
  BOOST_ASIO_INITFN_RESULT_TYPE(BOOST_ASIO_MOVE_ARG(async_handshake_completion_handler), void(const boost::system::error_code&))
  async_handshake(const std::string& host, const std::string& target, BOOST_ASIO_MOVE_ARG(async_handshake_completion_handler) handler);

  template <typename async_accept_completion_handler>
  BOOST_ASIO_INITFN_RESULT_TYPE(BOOST_ASIO_MOVE_ARG(async_accept_completion_handler), void(const boost::system::error_code&))
  async_accept(BOOST_ASIO_MOVE_ARG(async_accept_completion_handler) handler);

 private:
  template <typename T>
  class async_handshake_operation;

  template <typename T>
  class async_accept_operation;

  stream m_next_layer;

  core::allocator::ptr m_allocator;

  boost::optional<boost::beast::websocket::stream_base::decorator> m_decorator;
};

template <class stream>
template <typename T>
class upgrade<stream>::async_handshake_operation : public std::enable_shared_from_this<async_handshake_operation<T>> {
 public:
  using handler_type = typename std::remove_reference<T>::type;

  async_handshake_operation(stream& next_layer, core::allocator::ptr allocator, const std::string& host, const std::string& target, T&& handler);

  void run();

  void do_write_request();

  void on_write_request(const boost::system::error_code& error_code, std::size_t length);

  void do_read_response();

  void on_read_response(const boost::system::error_code& error_code, std::size_t length);

  void on_error(const boost::system::error_code& error_code);

  void on_complete();

 private:
  stream& m_next_layer;

  core::allocator::ptr m_allocator;

  rstream::core::logger m_logger;

  const std::string m_host;

  const std::string m_target;

  handler_type m_handler;

  boost::beast::basic_flat_buffer<core::allocator::wrapper<char>> m_buffer;

  boost::beast::http::request<boost::beast::http::empty_body> m_request;

  boost::beast::http::response<boost::beast::http::empty_body> m_response;
};

template <class stream>
template <typename T>
class upgrade<stream>::async_accept_operation : public std::enable_shared_from_this<async_accept_operation<T>> {
 public:
  using handler_type = typename std::remove_reference<T>::type;

  async_accept_operation(stream& next_layer, core::allocator::ptr allocator, T&& handler);

  void run();

  void do_read_request();

  void on_read_request(const boost::system::error_code& error_code, std::size_t length);

  void do_write_response();

  void on_write_response(const boost::system::error_code& error_code, std::size_t length);

  void on_error(const boost::system::error_code& error_code);

  void on_complete();

 private:
  stream& m_next_layer;

  core::allocator::ptr m_allocator;

  rstream::core::logger m_logger;

  handler_type m_handler;

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
void upgrade<stream>::set_decorator(boost::beast::websocket::stream_base::decorator decorator)
{
  m_decorator = decorator;
}

template <class stream>
template <typename handshake_handler>
BOOST_ASIO_INITFN_RESULT_TYPE(BOOST_ASIO_MOVE_ARG(handshake_handler), void(const boost::system::error_code&))
upgrade<stream>::async_handshake(const std::string& host, const std::string& target, BOOST_ASIO_MOVE_ARG(handshake_handler) handler)
{
  return boost::asio::async_initiate<handshake_handler, void(const boost::system::error_code&)>(
      [this](auto&& handler, const std::string& host, const std::string& target) {
        std::allocate_shared<async_handshake_operation<decltype(handler)>>(core::allocator::wrapper<async_handshake_operation<decltype(handler)>>(m_allocator), m_next_layer, m_allocator, host, target, std::forward<decltype(handler)>(handler))->run();
      },
      handler, host, target);
}

template <class stream>
template <typename accept_handler>
BOOST_ASIO_INITFN_RESULT_TYPE(BOOST_ASIO_MOVE_ARG(accept_handler), void(const boost::system::error_code&))
upgrade<stream>::async_accept(BOOST_ASIO_MOVE_ARG(accept_handler) handler)
{
  return boost::asio::async_initiate<accept_handler, void(const boost::system::error_code&)>(
      [this](auto&& handler) {
        std::allocate_shared<async_accept_operation<decltype(handler)>>(core::allocator::wrapper<async_accept_operation<decltype(handler)>>(m_allocator), m_next_layer, m_allocator, std::forward<decltype(handler)>(handler))->run();
      },
      handler);
}

template <class stream>
template <typename T>
upgrade<stream>::async_handshake_operation<T>::async_handshake_operation(stream& next_layer, core::allocator::ptr allocator, const std::string& host, const std::string& target, T&& handler)
    : m_next_layer(next_layer),
      m_allocator(allocator),
      m_logger({"rstream", "io", "http", "upgrade", "handshake", fmt::format("#{}", fmt::ptr(this))}),
      m_host(host),
      m_target(target),
      m_handler(std::forward<decltype(handler)>(handler)),
      m_buffer(allocator)

{
}

template <class stream>
template <typename T>
void upgrade<stream>::async_handshake_operation<T>::run()
{
  do_write_request();
}

template <class stream>
template <typename T>
void upgrade<stream>::async_handshake_operation<T>::do_write_request()
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
#ifdef DEBUG_BUILD
  {
    std::stringstream str;
    str << m_request.base();
    str.str(str.str().substr(0, str.str().find_last_not_of("\r\n") + 1));
    m_logger->trace("sending HTTP request :\n{}", str.str());
  }
#endif
  // send the request
  auto completion_handler = std::bind(&async_handshake_operation::on_write_request, async_handshake_operation::shared_from_this(), std::placeholders::_1, std::placeholders::_2);
  boost::beast::http::async_write(m_next_layer, m_request, completion_handler);
}

template <class stream>
template <typename T>
void upgrade<stream>::async_handshake_operation<T>::on_write_request(const boost::system::error_code& error_code, std::size_t length)
{
  (void)length;
  if (error_code) {
    on_error(error_code);
  }
  else {
    do_read_response();
  }
}

template <class stream>
template <typename T>
void upgrade<stream>::async_handshake_operation<T>::do_read_response()
{
  m_response              = {};
  auto completion_handler = std::bind(&async_handshake_operation::on_read_response, async_handshake_operation::shared_from_this(), std::placeholders::_1, std::placeholders::_2);
  boost::beast::http::async_read(m_next_layer, m_buffer, m_response, completion_handler);
}

template <class stream>
template <typename T>
void upgrade<stream>::async_handshake_operation<T>::on_read_response(const boost::system::error_code& error_code, std::size_t length)
{
  (void)length;
  if (error_code) {
    on_error(error_code);
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
      on_complete();
    }
    else {
      on_error(boost::beast::http::error::bad_status);
    }
  }
}

template <class stream>
template <typename T>
void upgrade<stream>::async_handshake_operation<T>::on_error(const boost::system::error_code& error_code)
{
  rstream::core::invoke_completion_handler(m_next_layer.get_executor(), std::move(m_handler), error_code);
}

template <class stream>
template <typename T>
void upgrade<stream>::async_handshake_operation<T>::on_complete()
{
  rstream::core::invoke_completion_handler(m_next_layer.get_executor(), std::move(m_handler), boost::system::error_code());
}

template <class stream>
template <typename T>
upgrade<stream>::async_accept_operation<T>::async_accept_operation(stream& next_layer, core::allocator::ptr allocator, T&& handler)
    : m_next_layer(next_layer),
      m_allocator(allocator),
      m_logger({"rstream", "io", "http", "upgrade", "accept", fmt::format("#{}", fmt::ptr(this))}),
      m_handler(std::forward<decltype(handler)>(handler)),
      m_buffer(allocator)

{
}

template <class stream>
template <typename T>
void upgrade<stream>::async_accept_operation<T>::run()
{
  do_read_request();
}

template <class stream>
template <typename T>
void upgrade<stream>::async_accept_operation<T>::do_read_request()
{
  m_request               = {};
  auto completion_handler = std::bind(&async_accept_operation::on_read_request, async_accept_operation::shared_from_this(), std::placeholders::_1, std::placeholders::_2);
  boost::beast::http::async_read(m_next_layer, m_buffer, m_request, completion_handler);
}

template <class stream>
template <typename T>
void upgrade<stream>::async_accept_operation<T>::on_read_request(const boost::system::error_code& error_code, std::size_t length)
{
  (void)length;
  if (error_code) {
    on_error(error_code);
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
    do_write_response();
  }
}

template <class stream>
template <typename T>
void upgrade<stream>::async_accept_operation<T>::do_write_response()
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
#ifdef DEBUG_BUILD
  {
    std::stringstream str;
    str << m_response.base();
    str.str(str.str().substr(0, str.str().find_last_not_of("\r\n") + 1));
    m_logger->trace("sending HTTP response :\n{}", str.str());
  }
#endif
  // send the response
  auto completion_handler = std::bind(&async_accept_operation::on_write_response, async_accept_operation::shared_from_this(), std::placeholders::_1, std::placeholders::_2);
  boost::beast::http::async_write(m_next_layer, m_response, completion_handler);
}

template <class stream>
template <typename T>
void upgrade<stream>::async_accept_operation<T>::on_write_response(const boost::system::error_code& error_code, std::size_t length)
{
  (void)length;
  if (error_code) {
    on_error(error_code);
  }
  else {
    if (m_response.result() == boost::beast::http::status::switching_protocols) {
      on_complete();
    }
    else {
      on_error(boost::beast::http::error::bad_status);
    }
  }
}

template <class stream>
template <typename T>
void upgrade<stream>::async_accept_operation<T>::on_error(const boost::system::error_code& error_code)
{
  rstream::core::invoke_completion_handler(m_next_layer.get_executor(), std::move(m_handler), error_code);
}

template <class stream>
template <typename T>
void upgrade<stream>::async_accept_operation<T>::on_complete()
{
  rstream::core::invoke_completion_handler(m_next_layer.get_executor(), std::move(m_handler), boost::system::error_code());
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
