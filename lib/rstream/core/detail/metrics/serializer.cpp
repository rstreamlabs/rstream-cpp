// See LICENSE file in the project root for license information.

#include "serializer.hpp"

#include <cmath>
#include <limits>
#include <locale>
#include <ostream>
#include <sstream>
#include <string>

#include <boost/algorithm/string/predicate.hpp>
#include <boost/optional.hpp>

#include <rstream/config.hpp>

namespace rstream {
namespace core {
namespace detail {
namespace metrics {

namespace impl RSTREAM_GNUC_INTERNAL {

template <typename T>
struct extra_label {
  std::string m_key;
  T m_value;
};

template <class T>
void serialize_value(std::ostream& out, const T& value)
{
  out << value;
}

template <>
void serialize_value<double>(std::ostream& out, const double& value)
{
  if (std::isnan(value)) {
    out << "Nan";
  }
  else if (std::isinf(value)) {
    out << (value < 0 ? "-Inf" : "+Inf");
  }
  else {
    out << value;
  }
}

template <>
void serialize_value<std::string>(std::ostream& out, const std::string& value)
{
  for (auto c : value) {
    switch (c) {
      case '\n': {
        out << '\\' << 'n';
      } break;
      case '\\': {
        out << '\\' << c;
      } break;
      case '"': {
        out << '\\' << c;
      } break;
      default: {
        out << c;
      } break;
    }
  }
}

template <class T = double>
void serialize_line_front(std::ostream& out, const std::string& name, const sample& sample, const std::string& suffix = "", const boost::optional<extra_label<T>>& extra_label = {})
{
  out << name << suffix << "{";
  int i = 0;
  for (auto it = sample.m_labels.cbegin(); it != sample.m_labels.cend(); ++it) {
    out << (i == 0 ? "" : ",") << it->first << "=\"";
    serialize_value(out, it->second);
    out << "\"";
    i++;
  }
  if (extra_label) {
    out << (i == 0 ? "" : ",") << extra_label.get().m_key << "=\"";
    serialize_value(out, extra_label.get().m_value);
    out << "\"";
    i++;
  }
  out << "}";
  out << " ";
}

void serialize_line_back(std::ostream& out, const sample&)
{
  //        auto epoch = std::chrono::duration_cast<std::chrono::milliseconds>(sample.m_timestamp.time_since_epoch()).count();
  //        if (epoch != 0) {
  //            out << " " << epoch;
  //        }
  out << "\n";
}

void serialize_metric_counter(std::ostream& out, const metric& metric)
{
  out << "# TYPE " << metric.m_name << " counter\n";
  for (const auto& sample : metric.m_samples) {
    serialize_line_front(out, metric.m_name, sample);
    serialize_value(out, boost::get<double>(sample.m_value));
    serialize_line_back(out, sample);
  }
}

void serialize_metric_gauge(std::ostream& out, const metric& metric)
{
  out << "# TYPE " << metric.m_name << " gauge\n";
  for (const auto& sample : metric.m_samples) {
    serialize_line_front(out, metric.m_name, sample);
    serialize_value(out, boost::get<double>(sample.m_value));
    serialize_line_back(out, sample);
  }
}

void serialize_metric_histogram(std::ostream& out, const metric& metric)
{
  out << "# TYPE " << metric.m_name << " histogram\n";
  for (const auto& sample : metric.m_samples) {
    const auto& histogram = boost::get<sample::histogram>(sample.m_value);
    auto last             = -std::numeric_limits<double>::infinity();
    for (const auto& bucket : histogram.m_buckets) {
      serialize_line_front<double>(out, metric.m_name, sample, "_bucket", extra_label<double>{.m_key = "le", .m_value = bucket.m_upper_bound});
      last = bucket.m_upper_bound;
      serialize_value(out, bucket.m_cumulative_count);
      serialize_line_back(out, sample);
    }
    if (last != std::numeric_limits<double>::infinity()) {
      serialize_line_front<std::string>(out, metric.m_name, sample, "_bucket", extra_label<std::string>{.m_key = "le", .m_value = "+Inf"});
      out << histogram.m_sample_count;
      serialize_line_back(out, sample);
    }
    serialize_line_front(out, metric.m_name, sample, "_sum");
    serialize_value(out, histogram.m_sample_sum);
    serialize_line_back(out, sample);
    serialize_line_front(out, metric.m_name, sample, "_count");
    serialize_value(out, histogram.m_sample_count);
    serialize_line_back(out, sample);
  }
}

void serialize_metric_summary(std::ostream& out, const metric& metric)
{
  out << "# TYPE " << metric.m_name << " summary\n";
  for (const auto& sample : metric.m_samples) {
    const auto& summary = boost::get<sample::summary>(sample.m_value);
    for (const auto& quantile : summary.m_quantiles) {
      serialize_line_front<double>(out, metric.m_name, sample, "", extra_label<double>{.m_key = "quantile", .m_value = quantile.m_quantile});
      serialize_value(out, quantile.m_value);
      serialize_line_back(out, sample);
    }
    serialize_line_front(out, metric.m_name, sample, "_sum");
    serialize_value(out, summary.m_sample_sum);
    serialize_line_back(out, sample);
    serialize_line_front(out, metric.m_name, sample, "_count");
    serialize_value(out, summary.m_sample_count);
    serialize_line_back(out, sample);
  }
}

void serialize_metric_info(std::ostream& out, const metric& metric)
{
  out << "# TYPE " << metric.m_name << " gauge\n";
  for (const auto& sample : metric.m_samples) {
    serialize_line_front(out, metric.m_name, sample);
    serialize_value(out, (double)1.0);
    serialize_line_back(out, sample);
  }
}

template <>
void serialize_value(std::ostream& out, const metric& value)
{
  if (!value.m_help.empty()) {
    out << "# HELP " << value.m_name << " " << value.m_help << "\n";
  }
  switch (value.m_type) {
    case metric::type::counter: {
      serialize_metric_counter(out, value);
    } break;
    case metric::type::gauge: {
      serialize_metric_gauge(out, value);
    } break;
    case metric::type::histogram: {
      serialize_metric_histogram(out, value);
    } break;
    case metric::type::summary: {
      serialize_metric_summary(out, value);
    } break;
    case metric::type::info: {
      serialize_metric_info(out, value);
    } break;
    default:
      break;
  }
}

template <>
void serialize_value<metrics>(std::ostream& out, const metrics& value)
{
  auto saved_loc       = out.getloc();
  auto saved_precision = out.precision();
  out.imbue(std::locale::classic());
  out.precision(std::numeric_limits<double>::max_digits10 - 1);
  for (const auto& metric : value) {
    serialize_value(out, metric);
  }
  out.imbue(saved_loc);
  out.precision(saved_precision);
}

}  // namespace impl RSTREAM_GNUC_INTERNAL

std::string serializer::operator()(const metrics& metrics) const
{
  std::stringstream str;
  impl::serialize_value(str, metrics);
  return str.str();
}

}  // namespace metrics
}  // namespace detail
}  // namespace core
}  // namespace rstream
