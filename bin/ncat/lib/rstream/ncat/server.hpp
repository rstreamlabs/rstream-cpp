// See LICENSE file in the project root for license information.

#pragma once

#include <memory>
#include <string>

#include <boost/variant.hpp>

#include <rstream/core/completion_handler.hpp>
#include <rstream/io/address.hpp>
#include <rstream/io/io_object.hpp>

#include "ncat.hpp"

namespace rstream {
namespace ncat {

class server : public io::io_object {
 public:
  struct exec {
    bool m_shell;
    std::string m_cmd;
  };

  struct config {
    io::address m_local;
    boost::variant<io::address, exec> m_remote;
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

}  // namespace ncat
}  // namespace rstream
