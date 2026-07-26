// See LICENSE file in the project root for license information.

#include "info.hpp"

#include <shared_mutex>

#include <rstream/config.hpp>

namespace rstream {
namespace core {
namespace detail {
namespace metrics {

template <>
class RSTREAM_GNUC_INTERNAL wrapper<info>::impl : public wrapper<info>::handle {
 public:
  impl(const wrapper_common::description& description, const detail::metrics::labels& labels)
      : wrapper<info>::handle(description, labels)
  {
  }
  impl(wrapper_common::ptr parent, const detail::metrics::labels& labels)
      : wrapper<info>::handle(parent, labels)
  {
  }
  void set(const info::value_type& value, const examplar& examplar);
  sample get_sample() override;

 private:
  std::shared_mutex m_mutex;
  info::value_type m_value;
  timestamp m_timestamp;
  examplar m_examplar;
};

template <>
std::shared_ptr<wrapper_base<info>> wrapper<info>::get_base() const
{
  return m_impl;
}

info::info(const std::string& name, const std::string& help, const detail::metrics::labels& labels, registry::ptr registry)
    : wrapper(wrapper_common::description(metric::type::info, name, help), labels, registry)
{
}

info::info(wrapper_common::ptr parent, const detail::metrics::labels& labels)
    : wrapper(parent, labels)
{
}

info::info(std::shared_ptr<impl> impl)
    : wrapper(impl)
{
}

void info::set(const info::value_type& value, const examplar& examplar)
{
  return get_impl()->set(value, examplar);
}

void wrapper<info>::impl::set(const info::value_type& value, const examplar& examplar)
{
  check_label_name_overlap(value, wrapper_common::get_labels(true));
  std::unique_lock lock(m_mutex);
  m_value     = value;
  m_timestamp = timestamp::clock::now();
  m_examplar  = examplar;
}

sample wrapper<info>::impl::get_sample()
{
  std::shared_lock lock(m_mutex);
  auto labels = wrapper_common::get_labels();
  labels.insert(m_value.cbegin(), m_value.cend());
  return sample{
      .m_value     = {},
      .m_labels    = labels,
      .m_timestamp = m_timestamp,
      .m_examplar  = m_examplar,
  };
}

}  // namespace metrics
}  // namespace detail
}  // namespace core
}  // namespace rstream
