// See LICENSE file in the project root for license information.

#pragma once

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include <boost/optional.hpp>

#include <rstream/io/address.hpp>

namespace rstream {
namespace io_rstrm {
namespace detail {

inline std::string join_stable_domain_labels(const std::vector<std::string>& labels, std::size_t offset)
{
  std::string result;
  for (std::size_t i = offset; i < labels.size(); ++i) {
    if (!result.empty()) {
      result += '.';
    }
    result += labels[i];
  }
  return result;
}

inline std::vector<std::string> split_stable_domain_labels(const std::string& host)
{
  std::vector<std::string> labels;
  std::string label;
  std::istringstream stream(host);
  while (std::getline(stream, label, '.')) {
    labels.push_back(label);
  }
  return labels;
}

inline bool is_stable_domain_label(const std::string& label)
{
  if (label.empty() || label.size() > 63 || label.front() == '-' || label.back() == '-') {
    return false;
  }
  return std::all_of(label.begin(), label.end(), [](unsigned char c) {
    return std::islower(c) || std::isdigit(c) || c == '-';
  });
}

inline std::string random_stable_domain_slug()
{
  std::random_device random_device;
  std::uniform_int_distribution<unsigned int> dist(0, 255);
  std::ostringstream out;
  out << 'r' << std::hex << std::setfill('0');
  for (int i = 0; i < 4; ++i) {
    out << std::setw(2) << dist(random_device);
  }
  return out.str();
}

inline boost::optional<std::string> generate_stable_domain(const io::address& server_address)
{
  std::string host = server_address.host();
  std::transform(host.begin(), host.end(), host.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  while (!host.empty() && host.back() == '.') {
    host.pop_back();
  }
  if (host.empty() || host.find(':') != std::string::npos) {
    return {};
  }
  auto labels = split_stable_domain_labels(host);
  if (labels.size() < 2) {
    return {};
  }
  const auto& project_endpoint = labels.front();
  const auto cluster_domain    = join_stable_domain_labels(labels, 1);
  if (!is_stable_domain_label(project_endpoint)) {
    return {};
  }
  for (const auto& label : split_stable_domain_labels(cluster_domain)) {
    if (!is_stable_domain_label(label)) {
      return {};
    }
  }
  if (project_endpoint.size() >= 63) {
    return {};
  }
  const auto max_slug_len = 63 - project_endpoint.size() - 1;
  if (max_slug_len < 9) {
    return {};
  }
  auto slug = random_stable_domain_slug();
  if (slug.size() > max_slug_len) {
    slug.resize(max_slug_len);
  }
  return slug + "-" + project_endpoint + ".t." + cluster_domain;
}

}  // namespace detail
}  // namespace io_rstrm
}  // namespace rstream
