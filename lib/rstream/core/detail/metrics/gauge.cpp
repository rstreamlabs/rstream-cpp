// See LICENSE file in the project root for license information.

#include "gauge.hpp"

#include <ctime>
#include <shared_mutex>

#include <rstream/config.hpp>

namespace rstream {
namespace core {
namespace detail {
namespace metrics {

template <>
class RSTREAM_GNUC_INTERNAL wrapper<gauge>::impl : public wrapper<gauge>::handle {
 public:
  impl(const wrapper_common::description& description, const detail::metrics::labels& labels)
      : wrapper<gauge>::handle(description, labels)
  {
  }
  impl(wrapper_common::ptr parent, const detail::metrics::labels& labels)
      : wrapper<gauge>::handle(parent, labels)
  {
  }
  void increment(double value, const examplar& examplar);
  void decrement(double value, const examplar& examplar);
  void set(double value, const examplar& examplar);
  double value();
  sample get_sample() override;

 private:
  std::shared_mutex m_mutex;
  double m_value{0};
  timestamp m_timestamp;
  examplar m_examplar;
};

template <>
std::shared_ptr<wrapper_base<gauge>> wrapper<gauge>::get_base() const
{
  return m_impl;
}

gauge::gauge(const std::string& name, const std::string& help, const detail::metrics::labels& labels, registry::ptr registry)
    : wrapper(wrapper_common::description(metric::type::gauge, name, help), labels, registry)
{
}

gauge::gauge(wrapper_common::ptr parent, const detail::metrics::labels& labels)
    : wrapper(parent, labels)
{
}

gauge::gauge(std::shared_ptr<impl> impl)
    : wrapper(impl)
{
}

void gauge::increment(const examplar& examplar)
{
  return increment(1.0, examplar);
}

void gauge::increment(double value, const examplar& examplar)
{
  return get_impl()->increment(value, examplar);
}

void gauge::decrement(const examplar& examplar)
{
  return decrement(1.0, examplar);
}

void gauge::decrement(double value, const examplar& examplar)
{
  return get_impl()->decrement(value, examplar);
}

void gauge::set(double value, const examplar& examplar)
{
  return get_impl()->set(value, examplar);
}

void gauge::set_current_time(const examplar& examplar)
{
  const auto time = std::time(nullptr);
  set(static_cast<double>(time));
}

double gauge::value() const
{
  return get_impl()->value();
}

void wrapper<gauge>::impl::increment(double value, const examplar& examplar)
{
  std::unique_lock lock(m_mutex);
  m_value += value;
  m_timestamp = timestamp::clock::now();
  m_examplar  = examplar;
}

void wrapper<gauge>::impl::decrement(double value, const examplar& examplar)
{
  increment(-1.0 * value, examplar);
}

void wrapper<gauge>::impl::set(double value, const examplar& examplar)
{
  std::unique_lock lock(m_mutex);
  m_value     = value;
  m_timestamp = timestamp::clock::now();
  m_examplar  = examplar;
}

double wrapper<gauge>::impl::value()
{
  std::shared_lock lock(m_mutex);
  return m_value;
}

sample wrapper<gauge>::impl::get_sample()
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
