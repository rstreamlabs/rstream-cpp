// See LICENSE file in the project root for license information.

#pragma once

#include <memory>

#include <rstream/io/resolver_base.hpp>

#include "endpoint.hpp"
#include "stream.hpp"

namespace rstream {
namespace io {
namespace detail {
namespace stream {

class resolver : public resolver_base<endpoint> {
 public:
  resolver(const executor_type& executor);

  virtual ~resolver() = default;

  void cancel() override;

 private:
  class impl;

  resolver(resolver_ptr native_handle);

  resolver_const_ptr native_handle() const;

  resolver_ptr native_handle();

  void swap(resolver_ptr native_handle);

  void async_resolve_internal(const boost::urls::url& url, async_resolve_completion_handler&& handler) override;

  std::shared_ptr<impl> m_impl;
};

}  // namespace stream
}  // namespace detail
}  // namespace io
}  // namespace rstream
