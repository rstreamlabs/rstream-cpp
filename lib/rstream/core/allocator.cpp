// See LICENSE file in the project root for license information.

#include "allocator.hpp"

#include <rstream/config.hpp>

namespace rstream {
namespace core {

class RSTREAM_GNUC_INTERNAL default_allocator : public allocator {
 public:
  default_allocator() = default;
  void* allocate(std::size_t size) override;
  void deallocate(void* pointer) override;
};

void* default_allocator::allocate(std::size_t size)
{
  return ::operator new(size);
}

void default_allocator::deallocate(void* pointer)
{
  ::operator delete(pointer);
}

allocator::ptr default_allocator()
{
  static auto ptr = std::make_shared<class default_allocator>();
  return ptr;
}

}  // namespace core
}  // namespace rstream
