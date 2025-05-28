// See LICENSE file in the project root for license information.

#include <iostream>

#include <rstream/core/exception.hpp>

int main(int argc, char** argv)
{
  // 1. how to convert an exception pointer into a human readable string
  auto exception_ptr = std::make_exception_ptr(std::runtime_error("this is a runtime error"));
  std::cout << rstream::core::throwable::to_string(exception_ptr) << std::endl;
  // 2. more detailed example
  rstream::core::throwable throwable(exception_ptr);
  std::cout
      << "received exception of type '"
      << throwable.get_name()
      << "' containing message '"
      << throwable.get_message()
      << "'"
      << std::endl;
  // 3. please note that 'rstream::core::throwable' class is nullable
  if (!throwable) {
    std::cout << "throwable is undefined" << std::endl;
  }
  // 4. example of chained exceptions
  auto func = []() {
    try {
      try {
        try {
          throw std::logic_error("this is a logic error");
        }
        catch (...) {
          throw rstream::core::nested_error("operation C aborted", std::current_exception());
        }
      }
      catch (...) {
        throw rstream::core::nested_error("operation B aborted", std::current_exception());
      }
    }
    catch (...) {
      throw rstream::core::nested_error("operation A aborted", std::current_exception());
    }
  };
  func();
  return 0;
}
