// See LICENSE file in the project root for license information.

#include "io_object.hpp"

namespace rstream {
namespace io {

io_object::io_object(const executor_type& executor)
    : m_executor(executor)
{
}

io_object::executor_type io_object::get_executor() const
{
  return m_executor;
}

io_object::lowest_layer_type& io_object::lowest_layer()
{
  return *this;
}

}  // namespace io
}  // namespace rstream
