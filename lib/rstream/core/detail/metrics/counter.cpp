// See LICENSE file in the project root for license information.

#include "counter.hpp"

#include <shared_mutex>

#include <rstream/config.hpp>

namespace rstream {
namespace core {
namespace detail {
namespace metrics {

template <>
class RSTREAM_GNUC_INTERNAL wrapper<counter>::impl : public wrapper<counter>::handle {
 public:
  impl(const wrapper_common::description& description, const detail::metrics::labels& labels)
      : wrapper<counter>::handle(description, labels)
  {
  }
  impl(wrapper_common::ptr parent, const detail::metrics::labels& labels)
      : wrapper<counter>::handle(parent, labels)
  {
  }
  void increment(double value, const examplar& examplar);
  void decrement(double value, const examplar& examplar);
  double value();
  sample get_sample() override;

 private:
  std::shared_mutex m_mutex;
  double m_value{0};
  timestamp m_timestamp;
  examplar m_examplar;
};

template <>
std::shared_ptr<wrapper_base<counter>> wrapper<counter>::get_base() const
{
  return m_impl;
}

counter::counter(const std::string& name, const std::string& help, const detail::metrics::labels& labels, registry::ptr registry)
    : wrapper(wrapper_common::description(metric::type::counter, name, help), labels, registry)
{
}

counter::counter(wrapper_common::ptr parent, const detail::metrics::labels& labels)
    : wrapper(parent, labels)
{
}

counter::counter(std::shared_ptr<impl> impl)
    : wrapper(impl)
{
}

void counter::increment(const examplar& examplar)
{
  return increment(1.0, examplar);
}

void counter::increment(double value, const examplar& examplar)
{
  return get_impl()->increment(value, examplar);
}

void counter::decrement(const examplar& examplar)
{
  return decrement(1.0, examplar);
}

void counter::decrement(double value, const examplar& examplar)
{
  return get_impl()->decrement(value, examplar);
}

double counter::value() const
{
  return get_impl()->value();
}

void wrapper<counter>::impl::increment(double value, const examplar& examplar)
{
  std::unique_lock lock(m_mutex);
  m_value += value;
  m_timestamp = timestamp::clock::now();
  m_examplar  = examplar;
}

void wrapper<counter>::impl::decrement(double value, const examplar& examplar)
{
  increment(-1.0 * value, examplar);
}

double wrapper<counter>::impl::value()
{
  std::shared_lock lock(m_mutex);
  return m_value;
}

sample wrapper<counter>::impl::get_sample()
{
  std::shared_lock lock(m_mutex);
  return (sample){
      .m_value     = m_value,
      .m_labels    = wrapper_common::get_labels(),
      .m_timestamp = m_timestamp,
      .m_examplar  = m_examplar,
  };
}

}  // namespace metrics
}  // namespace detail
}  // namespace core
}  // namespace rstream
