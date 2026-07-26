// See LICENSE file in the project root for license information.

#pragma once

#include <functional>
#include <memory>
#include <string>

#include <rstream/core/completion_handler.hpp>
#include <rstream/io/io_object.hpp>

#include "stun.hpp"

namespace rstream {
namespace stun {

class server : public io::io_object {
 public:
  struct config {
#ifdef RSTREAM_WITH_GEOIP
    struct {
      bool m_enable;
      std::string m_database_location;
    } m_geoip;
#endif
    std::string m_host;
    std::string m_port;
    bool m_inet4;
    bool m_inet6;
  };

  server(const executor_type& executor, const config& config, const settings_server& settings);

  ~server() noexcept override;

  using async_run_completion_handler = rstream::core::completion_handler<void(const boost::system::error_code&)>;

  void async_run(async_run_completion_handler&& handler);

  void cancel();

 private:
  class impl;

  std::shared_ptr<impl> m_impl;
};

}  // namespace stun
}  // namespace rstream
