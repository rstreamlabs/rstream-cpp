// See LICENSE file in the project root for license information.

#pragma once

#include <functional>
#include <memory>
#include <string>

#include <boost/core/noncopyable.hpp>

#include <rstream/core/completion_handler.hpp>
#include <rstream/io/address.hpp>

#include "webtty.hpp"

namespace rstream {
namespace webtty {

class server : private boost::noncopyable {
 public:
  struct config {
    io::address m_address;
    protocol::type m_protocol_type;
  };

  server(const executor_type& executor, const config& config, const settings_server& settings);

  virtual ~server() noexcept;

  using async_run_completion_handler = rstream::core::completion_handler<void(const std::error_code&)>;

  void async_run(async_run_completion_handler&& handler);

  void cancel();

 private:
  class impl;

  std::shared_ptr<impl> m_impl;
};

}  // namespace webtty
}  // namespace rstream
