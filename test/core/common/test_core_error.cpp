// See LICENSE file in the project root for license information.

#include <cassert>
#include <string>

#include <boost/system/error_code.hpp>

#include <rstream/core/helpers/asio.hpp>

namespace {

class test_category : public boost::system::error_category {
 public:
  explicit test_category(const char* name)
      : m_name(name)
  {
  }

  const char* name() const noexcept override
  {
    return m_name;
  }

  std::string message(int code) const override
  {
    return std::to_string(code);
  }

 private:
  const char* m_name;
};

}  // namespace

int main()
{
  const test_category category_a("test.category");
  const test_category category_b("test.category");
  const test_category category_c("other.category");
  const boost::system::error_code expected(42, category_a);
  const boost::system::error_code same_logical_error(42, category_b);
  const boost::system::error_code different_value(43, category_b);
  const boost::system::error_code different_category(42, category_c);

  assert(expected != same_logical_error);
  assert(rstream::core::helpers::matches_error(same_logical_error, expected));
  assert(!rstream::core::helpers::matches_error(different_value, expected));
  assert(!rstream::core::helpers::matches_error(different_category, expected));
  assert(rstream::core::helpers::is_eof_error(boost::system::error_code(boost::asio::error::eof)));
  assert(!rstream::core::helpers::is_eof_error(boost::system::error_code()));
  return 0;
}
