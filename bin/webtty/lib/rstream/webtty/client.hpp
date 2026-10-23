// See LICENSE file in the project root for license information.

#pragma once

#include <memory>

#include <boost/asio/async_result.hpp>
#include <boost/core/noncopyable.hpp>

#include <rstream/core/completion_handler.hpp>
#include <rstream/io/address.hpp>

#include "webtty.hpp"

namespace rstream {
namespace webtty {

class client : private boost::noncopyable {
 public:
  struct config {
    io::address m_address;
    boost::optional<std::string> m_websocket_target = boost::none;
    boost::optional<std::string> m_auth_token       = boost::none;
    protocol::config m_protocol_config;
  };

  client(const executor_type& executor, const config& config, const settings_client& settings);

  virtual ~client() noexcept;

  using async_run_completion_handler = rstream::core::completion_handler<void(const std::error_code&, int)>;

  void async_run(async_run_completion_handler&& handler);

  // Sends one capability-checked stop request. Completion confirms the write,
  // not the process result; async_run still drains output and receives Close.
  // While connecting, the request waits for Ack. Cancellation before the write
  // leaves the session open; cancellation during a write closes the transport
  // because a partially written frame cannot safely be retried.
  template <typename CompletionToken>
  auto async_request_termination(CompletionToken&& token)
  {
    return boost::asio::async_initiate<CompletionToken, void(const std::error_code&)>(
        [this](auto&& handler) { request_termination(std::forward<decltype(handler)>(handler)); }, token);
  }

  void cancel();

 private:
  using termination_completion_handler = rstream::core::completion_handler<void(const std::error_code&)>;

  void request_termination(termination_completion_handler&& handler);

  class impl;

  std::shared_ptr<impl> m_impl;
};

}  // namespace webtty
}  // namespace rstream
