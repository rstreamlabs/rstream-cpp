// See LICENSE file in the project root for license information.

#include "element.hpp"

#include <mutex>
#include <sstream>

#include <boost/optional.hpp>

#include <rstream/config.hpp>

namespace rstream {
namespace core {
namespace detail {
namespace plugin {

class RSTREAM_GNUC_INTERNAL element::impl {
 public:
  virtual ~impl() = default;

  void initialize(const info& info, const std::shared_ptr<plugin>& parent);

 private:
  struct data {
    info m_info;
    std::shared_ptr<plugin> m_parent;
  };

  std::mutex m_mutex;

  boost::optional<data> m_data;
};

element::element()
{
  m_impl = std::make_shared<impl>();
}

void element::initialize(const info& info, const std::shared_ptr<plugin>& parent)
{
  m_impl->initialize(info, parent);
}

void element::impl::initialize(const info& info, const std::shared_ptr<plugin>& parent)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  if (m_data) {
    throw std::logic_error("object already initialized");
  }
  data data = {
      .m_info   = info,
      .m_parent = parent,
  };
  m_data = data;
}

std::ostream& operator<<(std::ostream& ostream, const element::info& info)
{
  ostream << "[name: " << info.m_name << "] [description: " << info.m_description << "]";
  return ostream;
}

}  // namespace plugin
}  // namespace detail
}  // namespace core
}  // namespace rstream
