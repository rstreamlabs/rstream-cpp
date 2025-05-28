// See LICENSE file in the project root for license information.

#include "summary.hpp"

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <limits>
#include <shared_mutex>

#include <rstream/config.hpp>

namespace rstream {
namespace core {
namespace detail {
namespace metrics {

template <>
class RSTREAM_GNUC_INTERNAL wrapper<summary>::impl : public wrapper<summary>::handle {
 public:
  impl(const wrapper_common::description& description, const detail::metrics::labels& labels, const quantiles& quantiles, const std::chrono::milliseconds& time_window_s, const std::size_t& age_buckets)
      : wrapper<summary>::handle(description, labels, quantiles, time_window_s, age_buckets),
        m_quantiles(quantiles),
        m_time_window_quantiles(quantiles, time_window_s, age_buckets)
  {
  }
  impl(wrapper_common::ptr parent, const detail::metrics::labels& labels, const quantiles& quantiles, const std::chrono::milliseconds& time_window_s, const std::size_t& age_buckets)
      : wrapper<summary>::handle(parent, labels),
        m_quantiles(quantiles),
        m_time_window_quantiles(quantiles, time_window_s, age_buckets)
  {
  }
  void observe(double value, const examplar& examplar);
  sample get_sample() override;

 private:
  const quantiles m_quantiles;
  std::shared_mutex m_mutex;
  double m_sum{0.0};
  std::uint64_t m_count{0};
  time_window_quantiles m_time_window_quantiles;
  timestamp m_timestamp;
  examplar m_examplar;
};

template <>
std::shared_ptr<wrapper_base<summary>> wrapper<summary>::get_base() const
{
  return m_impl;
}

summary::summary(const std::string& name, const std::string& help, const detail::metrics::labels& labels, registry::ptr registry, const quantiles& quantiles, const std::chrono::milliseconds& time_window_s, const std::size_t& age_buckets)
    : wrapper(wrapper_common::description(metric::type::summary, name, help), labels, registry, quantiles, time_window_s, age_buckets)
{
}

summary::summary(wrapper_common::ptr parent, const detail::metrics::labels& labels, const quantiles& quantiles, const std::chrono::milliseconds& time_window_s, const std::size_t& age_buckets)
    : wrapper(parent, labels, quantiles, time_window_s, age_buckets)
{
}

summary::summary(std::shared_ptr<impl> impl)
    : wrapper(impl)
{
}

void summary::observe(double value, const examplar& examplar)
{
  return get_impl()->observe(value, examplar);
}

quantiles summary::default_quantiles()
{
  return {{0.1}, {0.5}, {0.75}, {0.9}, {0.95}, {0.99}};
}

void wrapper<summary>::impl::observe(double value, const examplar& examplar)
{
  std::unique_lock lock(m_mutex);
  m_count++;
  m_sum += value;
  m_time_window_quantiles.insert(value);
  m_timestamp = timestamp::clock::now();
  m_examplar  = examplar;
}

sample wrapper<summary>::impl::get_sample()
{
  std::shared_lock lock(m_mutex);
  sample::summary value = {
      .m_sample_count = m_count,
      .m_sample_sum   = m_sum,
      .m_quantiles    = {},
  };
  value.m_quantiles.reserve(m_quantiles.size());
  for (const auto& quantile : m_quantiles) {
    value.m_quantiles.push_back(std::move((sample::quantile){
        .m_quantile = quantile.m_quantile,
        .m_value    = m_time_window_quantiles.get(quantile.m_quantile),
    }));
  }
  return (sample){
      .m_value     = std::move(value),
      .m_labels    = wrapper_common::get_labels(),
      .m_timestamp = m_timestamp,
      .m_examplar  = m_examplar,
  };
}

}  // namespace metrics
}  // namespace detail
}  // namespace core
}  // namespace rstream
