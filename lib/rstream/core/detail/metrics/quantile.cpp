// See LICENSE file in the project root for license information.

#include "quantile.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>

namespace rstream {
namespace core {
namespace detail {
namespace metrics {

quantile::quantile(double quantile, double error)
    : m_quantile(quantile),
      m_error(error),
      m_u(2.0 * m_error / (1.0 - m_quantile)),
      m_v(2.0 * m_error / m_quantile)
{
}

quantile::quantile(double quantile_)
    : quantile(quantile_, (1.0 - quantile_) / 10)
{
}

ckms_quantiles::item::item(double value, int lower_delta, int delta)
    : m_value(value),
      m_g(lower_delta),
      m_delta(delta)
{
}

ckms_quantiles::ckms_quantiles(const quantiles& quantiles)
    : m_quantiles(quantiles),
      m_count(0),
      m_buffer_count(0)
{
}

void ckms_quantiles::insert(double value)
{
  m_buffer[m_buffer_count] = value;
  ++m_buffer_count;
  if (m_buffer_count == m_buffer.size()) {
    insert_batch();
    compress();
  }
}

double ckms_quantiles::get(double q)
{
  insert_batch();
  compress();
  if (m_sample.empty()) {
    return std::numeric_limits<double>::quiet_NaN();
  }
  int rank_min       = 0;
  const auto desired = static_cast<int>(q * m_count);
  const auto bound   = desired + (allowable_error(desired) / 2);
  auto it            = m_sample.begin();
  decltype(it) prev;
  auto cur = it++;
  while (it != m_sample.end()) {
    prev = cur;
    cur  = it++;
    rank_min += prev->m_g;
    if (rank_min + cur->m_g + cur->m_delta > bound) {
      return prev->m_value;
    }
  }
  return m_sample.back().m_value;
}

void ckms_quantiles::reset()
{
  m_count = 0;
  m_sample.clear();
  m_buffer_count = 0;
}

double ckms_quantiles::allowable_error(int rank)
{
  auto size        = m_sample.size();
  double min_error = size + 1;
  for (const auto& q : m_quantiles) {
    double error;
    if (rank <= q.m_quantile * size) {
      error = q.m_u * (size - rank);
    }
    else {
      error = q.m_v * rank;
    }
    if (error < min_error) {
      min_error = error;
    }
  }
  return min_error;
}

bool ckms_quantiles::insert_batch()
{
  if (m_buffer_count == 0) {
    return false;
  }
  std::sort(m_buffer.begin(), m_buffer.begin() + m_buffer_count);
  std::size_t start = 0;
  if (m_sample.empty()) {
    m_sample.emplace_back(m_buffer[0], 1, 0);
    ++start;
    ++m_count;
  }
  std::size_t idx  = 0;
  std::size_t item = idx++;
  for (std::size_t i = start; i < m_buffer_count; ++i) {
    double v = m_buffer[i];
    while (idx < m_sample.size() && m_sample[item].m_value < v) {
      item = idx++;
    }
    if (m_sample[item].m_value > v) {
      --idx;
    }
    int delta;
    if (idx - 1 == 0 || idx + 1 == m_sample.size()) {
      delta = 0;
    }
    else {
      delta = static_cast<int>(std::floor(allowable_error(idx + 1))) + 1;
    }
    m_sample.emplace(m_sample.begin() + idx, v, 1, delta);
    m_count++;
    item = idx++;
  }
  m_buffer_count = 0;
  return true;
}

void ckms_quantiles::compress()
{
  if (m_sample.size() < 2) {
    return;
  }
  std::size_t idx = 0;
  std::size_t prev;
  std::size_t next = idx++;
  while (idx < m_sample.size()) {
    prev = next;
    next = idx++;
    if (m_sample[prev].m_g + m_sample[next].m_g + m_sample[next].m_delta <= allowable_error(idx - 1)) {
      m_sample[next].m_g += m_sample[prev].m_g;
      m_sample.erase(m_sample.begin() + prev);
    }
  }
}

time_window_quantiles::time_window_quantiles(const quantiles& quantiles, const std::chrono::milliseconds& time_window_s, const std::size_t& age_buckets)
    : m_ckms_quantiles(age_buckets, quantiles),
      m_current_bucket(0),
      m_last_rotation(clock::now()),
      m_rotation_interval(time_window_s / age_buckets)
{
}

double time_window_quantiles::get(double q)
{
  ckms_quantiles& current_bucket = rotate();
  return current_bucket.get(q);
}

void time_window_quantiles::insert(double value)
{
  rotate();
  for (auto& bucket : m_ckms_quantiles) {
    bucket.insert(value);
  }
}

ckms_quantiles& time_window_quantiles::rotate()
{
  auto delta = clock::now() - m_last_rotation;
  while (delta > m_rotation_interval) {
    m_ckms_quantiles[m_current_bucket].reset();
    if (++m_current_bucket >= m_ckms_quantiles.size()) {
      m_current_bucket = 0;
    }
    delta -= m_rotation_interval;
    m_last_rotation += m_rotation_interval;
  }
  return m_ckms_quantiles[m_current_bucket];
}

}  // namespace metrics
}  // namespace detail
}  // namespace core
}  // namespace rstream
