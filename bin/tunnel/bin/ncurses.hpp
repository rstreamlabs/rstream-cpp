// See LICENSE file in the project root for license information.

#pragma once

#include <memory>
#include <string>

#include <rstream/core/completion_handler.hpp>
#include <rstream/io-rstrm/endpoint.hpp>
#include <rstream/io/io_object.hpp>
#include <rstream/tunnel/proxy.hpp>

class ncurses : public rstream::io::io_object {
 public:
  ncurses(const executor_type& executor);

  ~ncurses() noexcept override;

  using async_run_completion_handler = rstream::core::completion_handler<void(const boost::system::error_code&)>;

  void async_run(async_run_completion_handler&& handler);

  void cancel();

  void join();

  void render_status(const rstream::tunnel::status_proxy& status);

  void render_new_connection(const rstream::io_rstrm::endpoint& endpoint);

 private:
  class impl;

  std::shared_ptr<impl> m_impl;
};
