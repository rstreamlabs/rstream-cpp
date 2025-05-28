// See LICENSE file in the project root for license information.

#include "resolver.hpp"

#include <boost/url.hpp>

#include <rstream/config.hpp>
#include <rstream/core/completion_handler.hpp>

#include "io-rstrm.hpp"

namespace rstream {
namespace io_rstrm {

class RSTREAM_GNUC_INTERNAL resolver::impl {
 public:
  impl(const executor_type& executor, core::allocator::ptr allocator);

  virtual ~impl() = default;

  void cancel();

  void async_resolve(const boost::urls::url& url, async_resolve_completion_handler&& handler);

 private:
  executor_type m_executor;
};

resolver::resolver(const executor_type& executor, core::allocator::ptr allocator)
    : io::resolver_base<endpoint>(executor)
{
  m_impl = std::allocate_shared<impl>(core::allocator::wrapper<impl>(allocator), executor, allocator);
}

void resolver::cancel()
{
  m_impl->cancel();
}

void resolver::async_resolve_internal(const boost::urls::url& url, async_resolve_completion_handler&& handler)
{
  m_impl->async_resolve(url, std::move(handler));
}

resolver::impl::impl(const executor_type& executor, core::allocator::ptr allocator)
    : m_executor(executor)
{
  (void)allocator;
}

void resolver::impl::cancel()
{
}

void resolver::impl::async_resolve(const boost::urls::url& url, async_resolve_completion_handler&& handler)
{
  if (!handler) {
    return;
  }
  resolver::resolver::results_type results;
  boost::system::error_code error_code;
  auto endpoint_result = make_endpoint(url);
  if (endpoint_result) {
    results.push_back({endpoint_result.value(), url});
  }
  else {
    error_code = endpoint_result.error();
  }
  rstream::core::invoke_completion_handler(m_executor, std::move(handler), error_code, results);
}

}  // namespace io_rstrm
}  // namespace rstream
