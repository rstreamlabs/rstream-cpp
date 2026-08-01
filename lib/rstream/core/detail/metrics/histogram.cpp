// See LICENSE file in the project root for license information.

#include "histogram.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <shared_mutex>

#include <rstream/config.hpp>

namespace rstream {
namespace core {
namespace detail {
namespace metrics {

template <class ForwardIterator>
bool is_strict_sorted(ForwardIterator first, ForwardIterator last);

template <>
class RSTREAM_GNUC_INTERNAL wrapper<histogram>::impl : public wrapper<histogram>::handle {
 public:
  using bucket  = std::uint64_t;
  using buckets = std::vector<bucket>;
  impl(const wrapper_common::description& description, const detail::metrics::labels& labels, const histogram::upper_boundaries& upper_boundaries)
      : wrapper<histogram>::handle(description, labels, upper_boundaries),
        m_upper_boundaries(upper_boundaries),
        m_buckets(upper_boundaries.size() + 1, 0)
  {
    init();
  }
  impl(wrapper_common::ptr parent, const detail::metrics::labels& labels, const histogram::upper_boundaries& upper_boundaries)
      : wrapper<histogram>::handle(parent, labels),
        m_upper_boundaries(upper_boundaries),
        m_buckets(upper_boundaries.size() + 1, 0)
  {
    init();
  }
  void observe(double value, const examplar& examplar);
  sample get_sample() override;

 private:
  void init();
  const histogram::upper_boundaries m_upper_boundaries;
  std::shared_mutex m_mutex;
  buckets m_buckets;
  double m_sum{0.0};
  timestamp m_timestamp;
  examplar m_examplar;
};

template <>
std::shared_ptr<wrapper_base<histogram>> wrapper<histogram>::get_base() const
{
  return m_impl;
}

histogram::histogram(const std::string& name, const std::string& help, const detail::metrics::labels& labels, registry::ptr registry, const upper_boundaries& upper_boundaries)
    : wrapper(wrapper_common::description(metric::type::histogram, name, help), labels, registry, upper_boundaries)
{
}

histogram::histogram(wrapper_common::ptr parent, const detail::metrics::labels& labels, const upper_boundaries& upper_boundaries)
    : wrapper(parent, labels, upper_boundaries)
{
}

histogram::histogram(std::shared_ptr<impl> impl)
    : wrapper(impl)
{
}

void histogram::observe(double value, const examplar& examplar)
{
  return get_impl()->observe(value, examplar);
}

const histogram::upper_boundaries& histogram::default_upper_boundaries()
{
  static const upper_boundaries upper_boundaries = {0.005, 0.01, 0.025, 0.05, 0.075, 0.1, 0.25, 0.5, 0.75, 1.0, 2.5, 5.0, 7.5, 10.0};
  return upper_boundaries;
}

void wrapper<histogram>::impl::observe(double value, const examplar& examplar)
{
  std::unique_lock lock(m_mutex);
  auto index = static_cast<std::size_t>(std::distance(m_upper_boundaries.begin(), std::lower_bound(m_upper_boundaries.begin(), m_upper_boundaries.end(), value)));
  m_sum += value;
  m_buckets.at(index)++;
  m_timestamp = timestamp::clock::now();
  m_examplar  = examplar;
}

sample wrapper<histogram>::impl::get_sample()
{
  std::shared_lock lock(m_mutex);
  sample::histogram value = {
      .m_sample_count = 0,
      .m_sample_sum   = m_sum,
      .m_buckets      = {},
  };
  value.m_buckets.reserve(m_buckets.size());
  std::uint64_t cumulative_count = 0;
  for (auto it = m_buckets.cbegin(); it != m_buckets.cend(); ++it) {
    cumulative_count += *it;
    const auto index = static_cast<std::size_t>(std::distance(m_buckets.cbegin(), it));
    value.m_buckets.push_back(sample::bucket{
        .m_cumulative_count = cumulative_count,
        .m_upper_bound      = (index == m_upper_boundaries.size() ? std::numeric_limits<double>::infinity() : m_upper_boundaries.at(index)),
    });
  }
  value.m_sample_count = cumulative_count;
  return sample{
      .m_value     = std::move(value),
      .m_labels    = wrapper_common::get_labels(),
      .m_timestamp = m_timestamp,
      .m_examplar  = m_examplar,
  };
}

void wrapper<histogram>::impl::init()
{
  if (!is_strict_sorted(m_upper_boundaries.cbegin(), m_upper_boundaries.cend())) {
    throw std::invalid_argument("upper boundaries must be strictly sorted");
  }
}

template <class ForwardIterator>
bool is_strict_sorted(ForwardIterator first, ForwardIterator last)
{
  return std::adjacent_find(first, last, std::greater_equal<typename std::iterator_traits<ForwardIterator>::value_type>()) == last;
}

}  // namespace metrics
}  // namespace detail
}  // namespace core
}  // namespace rstream
