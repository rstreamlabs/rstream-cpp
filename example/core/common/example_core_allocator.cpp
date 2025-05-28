// See LICENSE file in the project root for license information.

#include <iostream>

#include <rstream/core/memory.hpp>

int main(int argc, char** argv)
{
  std::size_t length = 100;

  class my_allocator : public rstream::core::allocator {
   public:
    void* allocate(std::size_t length) override
    {
      auto pointer = malloc(length);
      std::cout << "allocating " << length << " byte(s) (#" << (uint64_t)pointer << ")" << std::endl;
      return pointer;
    }

    void deallocate(void* pointer) override
    {
      std::cout << "freing memory (#" << (uint64_t)pointer << ")" << std::endl;
      free(pointer);
    }
  };

  {
    auto memory = rstream::core::make_memory_allocated(length, std::make_shared<my_allocator>());
    std::cout << "user memory is accesible at #" << (uint64_t)memory.get_data() << std::endl;
    for (int i = 0; i < length; i++) {
      ((uint8_t*)memory.get_data())[i] = (i % 256);
    }
  }
  return 0;
}
