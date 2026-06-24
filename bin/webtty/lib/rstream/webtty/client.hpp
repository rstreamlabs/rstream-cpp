// See LICENSE file in the project root for license information.

#pragma once

#include <memory>

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
    boost::optional<std::string> m_websocket_target;
    boost::optional<std::string> m_auth_token;
    protocol::config m_protocol_config;
  };

  client(const executor_type& executor, const config& config, const settings_client& settings);

  virtual ~client();

  using async_run_completion_handler = rstream::core::completion_handler<void(const std::error_code&, int)>;

  void async_run(async_run_completion_handler&& handler);

  void cancel();

 private:
  class impl;

  std::shared_ptr<impl> m_impl;
};

}  // namespace webtty
}  // namespace rstream
