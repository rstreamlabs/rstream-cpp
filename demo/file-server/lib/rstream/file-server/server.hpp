// See LICENSE file in the project root for license information.

#pragma once

#include <functional>
#include <memory>
#include <string>

#include <rstream/core/completion_handler.hpp>
#include <rstream/io/address.hpp>
#include <rstream/io/io_object.hpp>

#include "file-server.hpp"

namespace rstream {
namespace file_server {

class server : public io::io_object {
 public:
  struct config {
    io::address m_address;
    std::string m_workdir;
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

}  // namespace file_server
}  // namespace rstream
