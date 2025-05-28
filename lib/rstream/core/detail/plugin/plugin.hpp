// See LICENSE file in the project root for license information.

#pragma once

#include <memory>
#include <ostream>
#include <string>
#include <utility>

#include <boost/date_time/gregorian/gregorian.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/optional.hpp>

#include "common.hpp"
#include "element.hpp"

#define RSTREAM_PLUGIN_SYMBOL get_plugin

namespace rstream {
namespace core {
namespace detail {
namespace plugin {

class factory;

class plugin {
  friend class factory;

 public:
  using ptr          = std::shared_ptr<plugin>;
  using name         = std::string;
  using description  = std::string;
  using version      = std::string;
  using license      = std::string;
  using release_date = boost::gregorian::date;
  using location     = boost::optional<boost::filesystem::path>;
  struct info {
    name m_name;
    description m_description;
    version m_version;
    license m_license;
    release_date m_release_date;
  };
  struct extended_info : info {
    type m_type;
    location m_location;
  };
  plugin(const location& location, const info& info);
  plugin(const plugin&)            = delete;
  plugin& operator=(const plugin&) = delete;
  const location& get_location() const;
  const info& get_info() const;
  extended_info get_extended_info() const;
  virtual const elements& get_elements() = 0;

 protected:
  const config& get_config();

 private:
  class impl;
  void initialize(const config& config, const shared_library& parent);
  virtual void init();
  const location m_location;
  const info m_info;
  std::shared_ptr<impl> m_impl;
};

class plugin_simple : public plugin {
 public:
  using descriptor = std::pair<info, elements>;
  plugin_simple(const location& location, const descriptor& descriptor);
  const elements& get_elements() override;

 private:
  const elements m_elements;
};

std::ostream& operator<<(std::ostream& ostream, const plugin::info& info);
std::ostream& operator<<(std::ostream& ostream, const plugin::extended_info& extended_info);

}  // namespace plugin
}  // namespace detail
}  // namespace core
}  // namespace rstream
