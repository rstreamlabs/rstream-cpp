// See LICENSE file in the project root for license information.

#include <iostream>
#include <sstream>

#include <rstream/config.hpp>
#include <rstream/core/metrics.hpp>

template <class T>
void compare(const T& current, const T& expected)
{
  if (current != expected) {
    std::stringstream stringstream;
    stringstream << "unexpected value [current: " << current << ", expected: " << expected << "]";
    throw std::runtime_error(stringstream.str());
  }
}

void test_1()
{
  std::cout << "running '" << RSTREAM_STRFUNC << "'" << std::endl;
  auto counter = rstream::core::metrics::counter("my_failures", "description of counter", {{"key1", "value1"}});
  compare(counter.name(), std::string("my_failures"));
  compare(counter.help(), std::string("description of counter"));
  compare(counter.value(), 0.0);
  counter.labels({{"key2", "value2"}}).increment(42.0);
  compare(counter.labels({{"key2", "value2"}}).value(), 42.0);
}

void run()
{
  test_1();
}

int main(int argc, char** argv)
{
  (void)argc;
  (void)argv;
  std::exception_ptr exception_ptr = nullptr;
  try {
    run();
  }
  catch (...) {
    exception_ptr = std::current_exception();
  }
  if (exception_ptr) {
    try {
      std::rethrow_exception(exception_ptr);
    }
    catch (const std::exception& exception) {
      std::cerr << "an error occurred: " << exception.what() << std::endl;
    }
  }
  return (exception_ptr ? -1 : 0);
}
