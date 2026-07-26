// See LICENSE file in the project root for license information.

#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <vector>

namespace rstream {
namespace core {
namespace detail {
namespace metrics {

struct quantile {
  quantile(double quantile, double error);
  quantile(double quantile);
  double m_quantile;
  double m_error;
  double m_u;
  double m_v;
};

using quantiles = std::vector<quantile>;

class ckms_quantiles {
 public:
  ckms_quantiles(const quantiles& quantiles);
  void insert(double value);
  double get(double q);
  void reset();

 private:
  struct item {
    item(double value, std::size_t lower_delta, std::size_t delta);
    double m_value;
    std::size_t m_g;
    std::size_t m_delta;
  };
  double allowable_error(std::size_t rank);
  bool insert_batch();
  void compress();
  const quantiles m_quantiles;
  std::size_t m_count;
  std::vector<item> m_sample;
  std::array<double, 500> m_buffer;
  std::size_t m_buffer_count;
};

class time_window_quantiles {
 public:
  using clock = std::chrono::steady_clock;
  time_window_quantiles(const quantiles& quantiles, const std::chrono::milliseconds& time_window_s, const std::size_t& age_buckets);
  double get(double q);
  void insert(double value);

 private:
  ckms_quantiles& rotate();
  std::vector<ckms_quantiles> m_ckms_quantiles;
  std::size_t m_current_bucket;
  clock::time_point m_last_rotation;
  clock::duration m_rotation_interval;
};

}  // namespace metrics
}  // namespace detail
}  // namespace core
}  // namespace rstream
