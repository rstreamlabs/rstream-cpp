// See LICENSE file in the project root for license information.

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <new>

#include <rstream/core/memory.hpp>

int main()
{
  std::size_t length = 100;

  class my_allocator : public rstream::core::allocator {
   public:
    void* allocate(std::size_t length) override
    {
      auto pointer = std::malloc(length);
      if (pointer == nullptr) {
        throw std::bad_alloc();
      }
      std::cout << "allocating " << length << " byte(s) (#" << pointer << ")" << std::endl;
      return pointer;
    }

    void deallocate(void* pointer) override
    {
      std::cout << "freeing memory (#" << pointer << ")" << std::endl;
      std::free(pointer);
    }
  };

  {
    auto memory = rstream::core::make_memory_allocated(length, std::make_shared<my_allocator>());
    std::cout << "user memory is accessible at #" << (uint64_t)memory.get_data() << std::endl;
    for (std::size_t i = 0; i < length; ++i) {
      static_cast<std::uint8_t*>(memory.get_data())[i] = (i % 256);
    }
  }
  return 0;
}
