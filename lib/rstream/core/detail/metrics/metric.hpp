// See LICENSE file in the project root for license information.

#pragma once

#include <list>
#include <string>
#include <vector>

#include <boost/variant.hpp>

#include "common.hpp"

namespace rstream {
namespace core {
namespace detail {
namespace metrics {

struct sample {
  struct quantile {
    double m_quantile;
    double m_value;
  };
  struct summary {
    std::uint64_t m_sample_count;
    double m_sample_sum;
    std::vector<quantile> m_quantiles;
  };
  struct bucket {
    std::uint64_t m_cumulative_count;
    double m_upper_bound;
  };
  struct histogram {
    std::uint64_t m_sample_count;
    double m_sample_sum;
    std::vector<bucket> m_buckets;
  };
  boost::variant<boost::blank, double, summary, histogram> m_value;
  labels m_labels;
  timestamp m_timestamp;
  examplar m_examplar;
};

using samples = std::list<sample>;

struct metric {
  enum class type {
    none,
    counter,
    gauge,
    histogram,
    summary,
    info
  };
  std::string m_name;
  std::string m_help;
  type m_type;
  samples m_samples;
};

using metrics = std::list<metric>;

}  // namespace metrics
}  // namespace detail
}  // namespace core
}  // namespace rstream
