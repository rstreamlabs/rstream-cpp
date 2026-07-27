// See LICENSE file in the project root for license information.

#pragma once

#include <sstream>
#include <string>

#include <boost/asio/async_result.hpp>
#include <boost/optional.hpp>

#include <rstream/core/allocator.hpp>
#include <rstream/core/buffer.hpp>
#include <rstream/core/completion_handler.hpp>
#include <rstream/core/detail/protobuf.hpp>
#include <rstream/core/helpers/protobuf.hpp>
#include <rstream/core/log.hpp>
#include <rstream/io-rstrm/error.hpp>
#include <rstream/io-rstrm/io-rstrm.hpp>
#include <rstream/io-rstrm/protobuf/messages.pb.h>
#include <rstream/io/address.hpp>
#include <rstream/io/payloader.hpp>

#include "convert.hpp"

namespace rstream {
namespace io_rstrm {
namespace detail {

template <class stream>
class handshake {
 public:
  using next_layer_type = typename std::remove_reference<stream>::type;

  enum class type {
    stream_req = 0,
    proxy_req  = 1,
  };

  template <typename arg_type>
  handshake(arg_type&& arg, const io::address& server_address, const config& config, core::allocator::ptr allocator = nullptr);

  template <typename arg_type>
  handshake(arg_type& arg, const io::address& server_address, const config& config, core::allocator::ptr allocator = nullptr);

  next_layer_type& next_layer();

  const next_layer_type& next_layer() const;

  template <typename async_run_completion_handler>
  auto async_run(type type, const std::string& id_name, const boost::optional<std::string>& token, BOOST_ASIO_MOVE_ARG(async_run_completion_handler) handler);

 private:
  template <typename T>
  class async_run_operation;

  stream m_next_layer;

  const io::address m_server_address;

  const config m_config;

  core::allocator::ptr m_allocator;
};

template <class stream>
template <typename T>
class handshake<stream>::async_run_operation : public std::enable_shared_from_this<async_run_operation<T>> {
 public:
  using handler_type = typename std::remove_reference<T>::type;

  async_run_operation(stream& next_layer, const io::address& server_address, const config& config, core::allocator::ptr allocator, type type, const std::string& id_name, const boost::optional<std::string>& token);

  void run(handler_type handler);

  void do_write_request(handler_type handler);

  void on_write_request(handler_type handler, const boost::system::error_code& error_code);

  void do_read_response(handler_type handler);

  void on_read_response(handler_type handler, const boost::system::error_code& error_code);

  void on_read_incoming_protobuf_message(handler_type handler, const protobuf::Message& message);

  void on_error(handler_type handler, const boost::system::error_code& error_code);

  void on_complete(handler_type handler);

 private:
  void payloader_control_cb(core::buffer& buffer, std::size_t size);

  using payloader_type = io::payloader<next_layer_type&>;

  stream& m_next_layer;

  const io::address m_server_address;

  const config m_config;

  core::allocator::ptr m_allocator;

  payloader_type m_payloader;

  core::logger m_logger;

  const type m_type;

  const std::string m_id_name;

  const boost::optional<std::string> m_token;

  core::buffer m_buffer;
};

template <class stream>
template <typename arg_type>
handshake<stream>::handshake(arg_type&& arg, const io::address& server_address, const config& config, core::allocator::ptr allocator)
    : m_next_layer(BOOST_ASIO_MOVE_CAST(arg_type)(arg)),
      m_server_address(server_address),
      m_config(config),
      m_allocator(allocator)
{
}

template <class stream>
template <typename arg_type>
handshake<stream>::handshake(arg_type& arg, const io::address& server_address, const config& config, core::allocator::ptr allocator)
    : m_next_layer(arg),
      m_server_address(server_address),
      m_config(config),
      m_allocator(allocator)
{
}

template <class stream>
typename handshake<stream>::next_layer_type& handshake<stream>::next_layer()
{
  return m_next_layer;
}

template <class stream>
const typename handshake<stream>::next_layer_type& handshake<stream>::next_layer() const
{
  return m_next_layer;
}

template <class stream>
template <typename run_handler>
auto handshake<stream>::async_run(type type, const std::string& id_name, const boost::optional<std::string>& token, BOOST_ASIO_MOVE_ARG(run_handler) handler)
{
  return boost::asio::async_initiate<run_handler, void(const boost::system::error_code&)>(
      [this](auto&& handler, enum type type, const std::string& id_name, const boost::optional<std::string>& token) {
        using operation_type     = async_run_operation<std::decay_t<decltype(handler)>>;
        auto operation_allocator = boost::asio::get_associated_allocator(handler);
        std::allocate_shared<operation_type>(
            operation_allocator,
            m_next_layer,
            m_server_address,
            m_config,
            m_allocator,
            type,
            id_name,
            token)
            ->run(std::forward<decltype(handler)>(handler));
      },
      handler, type, id_name, token);
}

template <class stream>
template <typename T>
handshake<stream>::async_run_operation<T>::async_run_operation(stream& next_layer, const io::address& server_address, const config& config, core::allocator::ptr allocator, type type, const std::string& id_name, const boost::optional<std::string>& token)
    : m_next_layer(next_layer),
      m_server_address(server_address),
      m_config(config),
      m_allocator(allocator),
      m_payloader(m_next_layer),
      m_logger({"rstream", "io-rstrm", "handshake", fmt::format("#{}", fmt::ptr(this))}),
      m_type(type),
      m_id_name(id_name),
      m_token(token),
      m_buffer(m_allocator)
{
  if (!m_config.m_zero_rtt) {
    m_payloader.set_control_callback(std::bind(&async_run_operation::payloader_control_cb, this, std::placeholders::_1, std::placeholders::_2));
  }
}

template <class stream>
template <typename T>
void handshake<stream>::async_run_operation<T>::run(handler_type handler)
{
  do_write_request(std::move(handler));
}

template <class stream>
template <typename T>
void handshake<stream>::async_run_operation<T>::do_write_request(handler_type handler)
{
  protobuf::Message message;
  boost::system::error_code error_code;
  {
    protobuf::ClientDetails proto_client_details;
    {
      auto client_details = m_token ? get_client_details(m_token.get()) : get_client_details(m_config, m_server_address);
      if (client_details) {
        detail::convert(proto_client_details, client_details.value());
      }
      else {
        error_code = client_details.error();
      }
    }
    if (!error_code) {
      bool is_secure = false;
#ifdef RSTREAM_WITH_IO_STREAMS
      is_secure = m_next_layer.is_secure();
#endif
      if (proto_client_details.has_token()) {
        if (!is_secure) {
#ifdef DEBUG_BUILD
          m_logger->warn("authentication token must not be sent over insecure connection");
#endif
          error_code = error::code::protocol_error;
        }
      }
      else {
        m_logger->info("no authentication token provided, please generate one on adminisration panel");
      }
    }
    if (!error_code) {
      if (m_type == type::stream_req) {
        protobuf::StreamReq req;
        req.set_tunnel_id_name(m_id_name);
        req.mutable_client_details()->CopyFrom(proto_client_details);
        if (m_config.m_zero_rtt) {
          google::protobuf::BoolValue value;
          value.set_value(true);
          req.mutable_zero_rtt()->CopyFrom(value);
        }
        message.mutable_stream_req()->CopyFrom(req);
      }
      else {
        protobuf::ProxyReq req;
        req.set_stream_id(m_id_name);
        req.mutable_client_details()->CopyFrom(proto_client_details);
        if (m_config.m_zero_rtt) {
          google::protobuf::BoolValue value;
          value.set_value(true);
          req.mutable_zero_rtt()->CopyFrom(value);
        }
        message.mutable_proxy_req()->CopyFrom(req);
      }
    }
  }
  if (error_code) {
    on_error(std::move(handler), error_code);
  }
  else {
#ifdef DEBUG_BUILD
    m_logger->trace("sending message to peer\n{}", core::helpers::to_json_string(message));
#endif
    core::buffer buffer;
    if (!core::detail::serialize_protobuf_message(message, buffer, m_allocator)) {
      on_error(std::move(handler), error::code::protocol_error);
      return;
    }
    auto ptr                = async_run_operation::shared_from_this();
    auto completion_handler = rstream::core::bind_associated_handler(
        std::move(handler),
        [ptr](auto& handler, const boost::system::error_code& error_code) mutable {
          ptr->on_write_request(std::move(handler), error_code);
        });
    m_payloader.async_send(buffer, std::move(completion_handler));
  }
}

template <class stream>
template <typename T>
void handshake<stream>::async_run_operation<T>::on_write_request(handler_type handler, const boost::system::error_code& error_code)
{
  if (error_code) {
    on_error(std::move(handler), error_code);
  }
  else if (m_config.m_zero_rtt) {
    on_complete(std::move(handler));
  }
  else {
    do_read_response(std::move(handler));
  }
}

template <class stream>
template <typename T>
void handshake<stream>::async_run_operation<T>::do_read_response(handler_type handler)
{
  auto ptr                = async_run_operation::shared_from_this();
  auto completion_handler = rstream::core::bind_associated_handler(
      std::move(handler),
      [ptr](auto& handler, const boost::system::error_code& error_code) mutable {
        ptr->on_read_response(std::move(handler), error_code);
      });
  m_payloader.async_recv(m_buffer, std::move(completion_handler));
}

template <class stream>
template <typename T>
void handshake<stream>::async_run_operation<T>::on_read_response(handler_type handler, const boost::system::error_code& error_code)
{
  if (error_code) {
    on_error(std::move(handler), error_code);
  }
  else {
    protobuf::Message message;
    if (core::detail::parse_protobuf_message(message, m_buffer.map().get_const_data(), m_buffer.get_size())) {
      on_read_incoming_protobuf_message(std::move(handler), message);
    }
    else {
#ifdef DEBUG_BUILD
      m_logger->warn("failed to parse incoming message");
#endif
      on_error(std::move(handler), error::code::protocol_error);
    }
  }
}

template <class stream>
template <typename T>
void handshake<stream>::async_run_operation<T>::on_read_incoming_protobuf_message(handler_type handler, const protobuf::Message& message)
{
  boost::system::error_code error_code;
#ifdef DEBUG_BUILD
  m_logger->trace("received message from peer\n{}", core::helpers::to_json_string(message));
#endif
  if (m_type == type::stream_req && message.has_stream_rsp()) {
    const auto& rsp = message.stream_rsp();
    if (rsp.has_error()) {
#ifdef DEBUG_BUILD
      std::stringstream ss;
      if (rsp.error().has_message()) {
        ss << " (" << rsp.error().message().value() << ")";
      }
      m_logger->warn("peer returned error code {}{}", static_cast<int>(rsp.error().code()), ss.str());
#endif
      error_code = error::make_error_code(static_cast<int>(rsp.error().code()));
    }
    else if (!rsp.has_stream_id()) {
#ifdef DEBUG_BUILD
      m_logger->warn("peer returned message with no payload");
#endif
      error_code = error::code::protocol_error;
    }
  }
  else if (m_type == type::proxy_req && message.has_proxy_rsp()) {
    const auto& rsp = message.proxy_rsp();
    if (rsp.has_error()) {
#ifdef DEBUG_BUILD
      std::stringstream ss;
      if (rsp.error().has_message()) {
        ss << " (" << rsp.error().message().value() << ")";
      }
      m_logger->warn("peer returned error code {}{}", static_cast<int>(rsp.error().code()), ss.str());
#endif
      error_code = error::make_error_code(static_cast<int>(rsp.error().code()));
    }
  }
  else {
#ifdef DEBUG_BUILD
    m_logger->warn("peer returned message with unexpected payload");
#endif
    error_code = error::code::protocol_error;
  }
  if (error_code) {
    on_error(std::move(handler), error_code);
  }
  else {
    on_complete(std::move(handler));
  }
}

template <class stream>
template <typename T>
void handshake<stream>::async_run_operation<T>::on_error(handler_type handler, const boost::system::error_code& error_code)
{
  rstream::core::invoke_completion_handler(m_next_layer.get_executor(), std::move(handler), error_code);
}

template <class stream>
template <typename T>
void handshake<stream>::async_run_operation<T>::on_complete(handler_type handler)
{
  rstream::core::invoke_completion_handler(m_next_layer.get_executor(), std::move(handler), boost::system::error_code());
}

template <class stream>
template <typename T>
void handshake<stream>::async_run_operation<T>::payloader_control_cb(core::buffer& buffer, std::size_t size)
{
  buffer.reset_size();
  std::size_t offset, maxsize;
  m_buffer.get_size(offset, maxsize);
  if (size > maxsize) {
    auto memory = core::make_memory_allocated(std::min(size - maxsize, m_config.m_max_buffer_size), m_allocator);
    buffer.append(memory);
  }
}

}  // namespace detail
}  // namespace io_rstrm
}  // namespace rstream
