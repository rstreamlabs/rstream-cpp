// See LICENSE file in the project root for license information.

#pragma once

#include <boost/asio/any_io_executor.hpp>

namespace rstream {
namespace io {

class io_object {
 public:
  using executor_type = boost::asio::any_io_executor;

  using lowest_layer_type = io_object;

  io_object(const executor_type& executor);

  virtual ~io_object() = default;

  executor_type get_executor() const;

  lowest_layer_type& lowest_layer();

 private:
  executor_type m_executor;
};

}  // namespace io
}  // namespace rstream
