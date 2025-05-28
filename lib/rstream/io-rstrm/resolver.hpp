// See LICENSE file in the project root for license information.

#pragma once

#include <memory>

#include <rstream/core/allocator.hpp>
#include <rstream/io/resolver_base.hpp>

#include "endpoint.hpp"

#include "io-rstrm.hpp"

namespace rstream {
namespace io_rstrm {

class resolver : public io::resolver_base<endpoint> {
 public:
  resolver(const executor_type& executor, core::allocator::ptr allocator = nullptr);

  virtual ~resolver() = default;

  void cancel() override;

 private:
  class impl;

  void async_resolve_internal(const boost::urls::url& url, async_resolve_completion_handler&& handler) override;

  std::shared_ptr<impl> m_impl;
};

}  // namespace io_rstrm
}  // namespace rstream
