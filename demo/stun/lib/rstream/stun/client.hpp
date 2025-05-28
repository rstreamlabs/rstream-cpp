// See LICENSE file in the project root for license information.

#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <type_traits>

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/async_result.hpp>
#include <boost/asio/bind_executor.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/system/system_error.hpp>

#include <rstream/core/completion_handler.hpp>
#include <rstream/core/log.hpp>
#include <rstream/io/io_object.hpp>

#include "message.hpp"

namespace rstream {
namespace stun {

class client_base {
 public:
  using executor_type = io::io_object::executor_type;
  struct config {
    config();
    std::size_t m_message_max_length;
  };
  client_base(const config& config);
  client_base();

 protected:
  class transport_base {
   public:
    using ptr = std::shared_ptr<transport_base>;
    static const rstream::core::logger& logger();
    virtual executor_type get_executor() const                                                                        = 0;
    using async_send_completion_handler                                                                               = rstream::core::completion_handler<void(const boost::system::error_code&, std::size_t)>;
    virtual void async_send(const boost::asio::const_buffer& buffer, async_send_completion_handler&& handler)         = 0;
    using async_receive_completion_handler                                                                            = rstream::core::completion_handler<void(const boost::system::error_code&, std::size_t)>;
    virtual void async_receive(const boost::asio::mutable_buffer& buffer, async_receive_completion_handler&& handler) = 0;
  };
  using async_request_completion_handler = rstream::core::completion_handler<void(const boost::system::error_code&, const message&)>;
  void async_request(const message& message, transport_base::ptr transport, async_request_completion_handler&& handler);

 private:
  class task;
  config m_config;
};

template <typename socket>
class client : public client_base {
 public:
  using endpoint        = typename std::remove_reference<socket>::type::endpoint_type;
  using next_layer_type = typename std::remove_reference<socket>::type;
  template <typename arg_type>
  client(arg_type&& arg);
  template <typename arg_type>
  client(arg_type& arg);
  next_layer_type& next_layer();
  const next_layer_type& next_layer() const;
  template <typename request_handler>
  BOOST_ASIO_INITFN_RESULT_TYPE(BOOST_ASIO_MOVE_ARG(request_handler), void(const boost::system::error_code&, const message&))
  async_request(const message& message, const endpoint& endpoint, BOOST_ASIO_MOVE_ARG(request_handler) handler);

 private:
  class transport : public transport_base {
   public:
    transport(socket& next_layer, const endpoint endpoint);
    executor_type get_executor() const override;
    void async_send(const boost::asio::const_buffer& buffer, async_send_completion_handler&& handler) override;
    void async_receive(const boost::asio::mutable_buffer& buffer, async_receive_completion_handler&& handler) override;

   private:
    socket& m_next_layer;
    endpoint m_endpoint;
  };
  typename transport::ptr make_transport(const endpoint endpoint);
  socket m_next_layer;
};

template <typename socket>
template <typename arg_type>
client<socket>::client(arg_type&& arg)
    : m_next_layer(BOOST_ASIO_MOVE_CAST(arg_type)(arg))
{
}

template <typename socket>
template <typename arg_type>
client<socket>::client(arg_type& arg)
    : m_next_layer(arg)
{
}

template <typename socket>
typename client<socket>::next_layer_type& client<socket>::next_layer()
{
  return m_next_layer;
}

template <typename socket>
const typename client<socket>::next_layer_type& client<socket>::next_layer() const
{
  return m_next_layer;
}

template <typename socket>
template <typename request_handler>
BOOST_ASIO_INITFN_RESULT_TYPE(BOOST_ASIO_MOVE_ARG(request_handler), void(const boost::system::error_code&, const message&))
client<socket>::async_request(const message& message, const endpoint& endpoint, BOOST_ASIO_MOVE_ARG(request_handler) handler)
{
  return boost::asio::async_initiate<request_handler, void(const boost::system::error_code&, const class message&)>(
      [this](auto&& handler, const class message& message, const client::endpoint& endpoint) {
        client_base::async_request(message, make_transport(endpoint), std::forward<decltype(handler)>(handler));
      },
      handler, message, endpoint);
}

template <typename socket>
typename client<socket>::transport::ptr client<socket>::make_transport(const endpoint endpoint)
{
  return std::make_shared<transport>(m_next_layer, endpoint);
}

template <typename socket>
client<socket>::transport::transport(socket& next_layer, const endpoint endpoint)
    : m_next_layer(next_layer),
      m_endpoint(endpoint)
{
}

template <typename socket>
client<socket>::executor_type client<socket>::transport::get_executor() const
{
  return m_next_layer.get_executor();
}

template <typename socket>
void client<socket>::transport::async_send(const boost::asio::const_buffer& buffer, async_send_completion_handler&& handler)
{
  logger()->trace("sending {} byte(s) to {}", buffer.size(), boost::lexical_cast<std::string>(m_endpoint));
  m_next_layer.async_send_to(buffer, m_endpoint, std::move(handler));
}

template <typename socket>
void client<socket>::transport::async_receive(const boost::asio::mutable_buffer& buffer, async_receive_completion_handler&& handler)
{
  auto executor           = boost::asio::get_associated_executor(handler, get_executor());
  auto completion_handler = [this, handler = std::move(handler)](const boost::system::error_code& error_code, std::size_t size) mutable {
    if (!error_code) {
      logger()->trace("received {} byte(s) from {}", size, boost::lexical_cast<std::string>(m_endpoint));
    }
    handler(error_code, size);
  };
  m_next_layer.async_receive_from(buffer, m_endpoint, boost::asio::bind_executor(executor, std::move(completion_handler)));
}

}  // namespace stun
}  // namespace rstream
