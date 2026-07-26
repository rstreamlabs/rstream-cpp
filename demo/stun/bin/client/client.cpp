// See LICENSE file in the project root for license information.

#include "client.hpp"

#include <chrono>

#include <rstream/core/completion_handler.hpp>
#include <rstream/core/log.hpp>
#include <rstream/stun/attribute.hpp>

client::client(const boost::asio::io_context::executor_type& executor, const config& config)
    : m_config(config),
      m_complete(false),
      m_executor(executor),
      m_resolver(executor),
      m_socket(executor),
      m_timer(executor),
      m_client(m_socket)
{
}

void client::async_run(async_run_completion_handler&& handler)
{
  m_handler.swap(handler);
  do_resolve_host();
  setup_timeout();
}

void client::cancel()
{
  m_error_code = (m_error_code ? m_error_code : rstream::io::error::code::operation_cancelled);
  clean();
}

void client::clean()
{
  boost::system::error_code tmp;
  m_resolver.cancel();
  m_socket.close(tmp);
  m_timer.cancel();
}

void client::do_resolve_host()
{
  using resolver_type     = boost::asio::ip::udp::resolver;
  auto completion_handler = std::bind(&client::on_do_resolve_host, shared_from_this(), std::placeholders::_1, std::placeholders::_2);
  auto flags              = resolver_type::flags::all_matching;
  m_resolver.async_resolve(m_config.m_host, m_config.m_port, flags, completion_handler);
}

void client::on_do_resolve_host(const boost::system::error_code& error_code, const boost::asio::ip::udp::resolver::results_type results)
{
  if (m_complete) {
    return;
  }
  auto code = m_error_code ? m_error_code : error_code;
  boost::asio::ip::udp::resolver::results_type::iterator it;
  if (!code) {
    for (it = results.begin(); it != results.end(); ++it) {
      if (m_config.m_inet4 && it->endpoint().address().is_v6()) {
        continue;
      }
      if (m_config.m_inet6 && it->endpoint().address().is_v4()) {
        continue;
      }
      break;
    }
    if (it == results.end()) {
      code = boost::system::error_code(boost::asio::error::address_family_not_supported);
    }
  }
  if (code) {
    on_error(code);
  }
  else {
    do_send_stun_request(*it);
  }
}

void client::do_send_stun_request(const boost::asio::ip::udp::endpoint& endpoint)
{
  m_socket.open(endpoint.protocol());
  if (m_config.m_address) {
    m_socket.bind(boost::asio::ip::udp::endpoint(m_config.m_address.get(), 0));
  }
  auto builder = rstream::stun::message_builder(rstream::stun::stun_class::request, rstream::stun::stun_method::binding);
  builder.add_software();
  builder.add_fingerprint();
  auto message            = builder.build();
  auto completion_handler = std::bind(&client::on_stun_response, shared_from_this(), std::placeholders::_1, std::placeholders::_2);
  m_client.async_request(message, endpoint, completion_handler);
}

void client::on_stun_response(const boost::system::error_code& error_code, const rstream::stun::message& message)
{
  if (m_complete) {
    return;
  }
  auto code = m_error_code ? m_error_code : error_code;
  if (!code) {
    std::exception_ptr exception_ptr = nullptr;
    try {
      on_result(rstream::stun::get_attribute<rstream::stun::attribute_value_xor_mapped_address>(message).get_address());
    }
    catch (...) {
      exception_ptr = std::current_exception();
    }
    if (exception_ptr) {
      try {
        std::rethrow_exception(exception_ptr);
      }
      catch (const boost::system::system_error& system_error) {
        code = system_error.code();
      }
      catch (const rstream::core::system_error& system_error) {
        code = system_error.code();
      }
      catch (...) {
        code = rstream::io::error::code::unknown_undefined_error;
      }
      if (code == rstream::io::error::code::unknown_undefined_error) {
        rstream::core::default_logger()->warn("error has unexpected type [{}]", rstream::core::throwable::to_string(exception_ptr));
      }
    }
  }
  if (code) {
    on_error(code);
  }
}

void client::setup_timeout()
{
  if (m_config.m_timeout_ms == 0) {
    return;
  }
  m_timer.expires_after(std::chrono::milliseconds(m_config.m_timeout_ms));
  auto completion_handler = std::bind(&client::on_timer_cb, shared_from_this(), std::placeholders::_1);
  m_timer.async_wait(completion_handler);
}

void client::on_timer_cb(const boost::system::error_code& error_code)
{
  if (error_code || m_complete) {
    return;
  }
  m_error_code = (m_error_code ? m_error_code : rstream::io::error::code::operation_timeout);
  cancel();
}

void client::on_error(const boost::system::error_code& error_code)
{
  m_complete = true;
  rstream::core::invoke_completion_handler(m_executor, std::move(m_handler), error_code, boost::asio::ip::address());
  m_handler = nullptr;
}

void client::on_result(const boost::asio::ip::address& address)
{
  m_complete = true;
  rstream::core::invoke_completion_handler(m_executor, std::move(m_handler), boost::system::error_code(), address);
  m_handler = nullptr;
}
