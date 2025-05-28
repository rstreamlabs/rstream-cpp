// See LICENSE file in the project root for license information.

#include "client.hpp"

#include <boost/asio/bind_executor.hpp>
#include <boost/asio/strand.hpp>

#include <rstream/config.hpp>
#include <rstream/core/completion_handler.hpp>
#include <rstream/core/log.hpp>

static const rstream::core::logger g_logger({"rstream", "stun", "client"});

namespace rstream {
namespace stun {

class RSTREAM_GNUC_INTERNAL client_base::task : public std::enable_shared_from_this<task> {
 public:
  task(const config& config, const message& message, transport_base::ptr transport, async_request_completion_handler&& handler);
  void run();

 private:
  void do_serialize_request();
  void do_send_request();
  void on_send_request(const boost::system::error_code& error_code, std::size_t);
  void do_read_response();
  void on_read_response(const boost::system::error_code& error_code, std::size_t length);
  void on_response(class message& response);
  void on_error(const boost::system::error_code& error_code);
  void on_complete(class message& response);
  boost::asio::strand<executor_type> m_strand;
  rstream::core::memory m_memory;
  const config m_config;
  const message m_request;
  const client_base::transport_base::ptr m_transport;
  async_request_completion_handler m_handler;
};

client_base::config::config()
{
  m_message_max_length = 500;
}

const rstream::core::logger& client_base::transport_base::logger()
{
  return g_logger;
}

client_base::client_base(const config& config)
    : m_config(config)
{
}

client_base::client_base()
    : client_base(config())
{
}

void client_base::async_request(const message& message, transport_base::ptr transport, async_request_completion_handler&& handler)
{
  std::make_shared<task>(m_config, message, transport, BOOST_ASIO_MOVE_CAST(async_request_completion_handler)(handler))->run();
}

client_base::task::task(const config& config, const message& message, transport_base::ptr transport, async_request_completion_handler&& handler)
    : m_strand(transport->get_executor()),
      m_config(config),
      m_request(message),
      m_transport(transport),
      m_handler(std::move(handler))

{
}

void client_base::task::run()
{
  do_serialize_request();
}

void client_base::task::do_serialize_request()
{
  const auto buffer_size  = m_config.m_message_max_length;
  const auto request_size = m_request.byte_size_long();
  if (request_size > buffer_size) {
    throw std::runtime_error("STUN request is too long");
  }
  m_memory = rstream::core::make_memory_allocated(buffer_size);
  m_request.basic_message::serialize(m_memory.get_data());
  m_memory.resize(0, request_size);
  do_send_request();
}

void client_base::task::do_send_request()
{
  g_logger->trace("sending STUN message :\n{}", m_request.to_string());
  auto ptr                = std::enable_shared_from_this<task>::shared_from_this();
  auto completion_handler = std::bind(&task::on_send_request, ptr, std::placeholders::_1, std::placeholders::_2);
  m_transport->async_send(boost::asio::const_buffer(m_memory.get_data(), m_memory.get_size()), boost::asio::bind_executor(m_strand, completion_handler));
}

void client_base::task::on_send_request(const boost::system::error_code& error_code, std::size_t)
{
  if (error_code) {
    on_error(error_code);
  }
  else {
    do_read_response();
  }
}

void client_base::task::do_read_response()
{
  m_memory.resize(0, m_config.m_message_max_length);
  auto ptr                = std::enable_shared_from_this<task>::shared_from_this();
  auto completion_handler = std::bind(&task::on_read_response, ptr, std::placeholders::_1, std::placeholders::_2);
  m_transport->async_receive(boost::asio::mutable_buffer(m_memory.get_data(), m_memory.get_size()), boost::asio::bind_executor(m_strand, completion_handler));
}

void client_base::task::on_read_response(const boost::system::error_code& error_code, std::size_t length)
{
  if (error_code) {
    on_error(error_code);
  }
  else {
    m_memory.resize(0, length);
    on_response(message().parse(m_memory));
  }
}

void client_base::task::on_response(class message& response)
{
  g_logger->trace("received STUN response :\n{}", response.to_string());
  if (response.get_header().get_transaction_id() == m_request.get_header().get_transaction_id()) {
    on_complete(response);
  }
  else {
    do_read_response();
  }
}

void client_base::task::on_error(const boost::system::error_code& error_code)
{
  rstream::core::invoke_completion_handler(m_transport->get_executor(), std::move(m_handler), error_code, message());
}

void client_base::task::on_complete(class message& response)
{
  rstream::core::invoke_completion_handler(m_transport->get_executor(), std::move(m_handler), boost::system::error_code(), response);
}

}  // namespace stun
}  // namespace rstream
