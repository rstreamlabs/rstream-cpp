// See LICENSE file in the project root for license information.

#include <iostream>
#include <list>
#include <memory>
#include <string>
#include <vector>

#include <rstream/core/allocator.hpp>

int main()
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
    rstream::core::allocator::wrapper<int> wrapper(std::make_shared<my_allocator>());
    std::vector<int, rstream::core::allocator::wrapper<int>> vector(length, wrapper);
  }
  {
    rstream::core::allocator::wrapper<int> wrapper(std::make_shared<my_allocator>());
    std::list<int, rstream::core::allocator::wrapper<int>> list(length, wrapper);
  }
  // the following does not compile on Linux : to be investigated ...
  //    {
  //        rstream::core::allocator::wrapper<char> wrapper(std::make_shared<my_allocator>());
  //        using string_type = std::basic_string<char, std::char_traits<char>, rstream::core::allocator::wrapper<char>>;
  //        string_type string("test", wrapper);
  //    }
  {
    rstream::core::allocator::wrapper<int> wrapper(std::make_shared<my_allocator>());
    std::shared_ptr<int> ptr = std::allocate_shared<int>(wrapper);
  }
  return 0;
}
