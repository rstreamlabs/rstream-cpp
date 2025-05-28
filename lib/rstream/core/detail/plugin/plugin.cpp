// See LICENSE file in the project root for license information.

#include "plugin.hpp"

#include <mutex>
#include <sstream>

#include <boost/optional.hpp>

#include <rstream/config.hpp>
#include <rstream/core/log.hpp>

namespace rstream {
namespace core {
namespace detail {
namespace plugin {

class RSTREAM_GNUC_INTERNAL plugin::impl {
 public:
  impl();

  virtual ~impl();

  void initialize(const config& config, const shared_library& parent);

  const config& get_config();

  type get_type();

 private:
  struct data {
    config m_config;
    shared_library m_parent;
  };

  rstream::core::logger m_logger;

  std::mutex m_mutex;

  boost::optional<data> m_data;
};

plugin::plugin(const location& location, const info& info)
    : m_location(location),
      m_info(info)
{
  m_impl = std::make_shared<impl>();
}

const plugin::location& plugin::get_location() const
{
  return m_location;
}

const plugin::info& plugin::get_info() const
{
  return m_info;
}

plugin::extended_info plugin::get_extended_info() const
{
  return (extended_info){get_info(), m_impl->get_type(), get_location()};
}

const config& plugin::get_config()
{
  return m_impl->get_config();
}

void plugin::initialize(const config& config, const shared_library& parent)
{
  return m_impl->initialize(config, parent);
}

void plugin::init()
{
}

plugin::impl::impl()
    : m_logger({"rstream", "core", "plugin", fmt::format("#{}", fmt::ptr(this))})
{
#ifdef DEBUG_BUILD
  m_logger->trace("plugin created");
#endif
}

plugin::impl::~impl()
{
#ifdef DEBUG_BUILD
  m_logger->trace("plugin destroyed");
#endif
}

void plugin::impl::initialize(const config& config, const shared_library& parent)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  if (m_data) {
    throw std::logic_error("object already initialized");
  }
  data data = {
      .m_config = config,
      .m_parent = parent,
  };
  m_data = data;
#ifdef DEBUG_BUILD
  m_logger->trace("plugin initialized");
#endif
}

const config& plugin::impl::get_config()
{
  std::lock_guard<std::mutex> lock(m_mutex);
  if (!m_data) {
    throw std::logic_error("object uninitialized");
  }
  return m_data->m_config;
}

type plugin::impl::get_type()
{
  std::lock_guard<std::mutex> lock(m_mutex);
  if (!m_data) {
    throw std::logic_error("object uninitialized");
  }
  return m_data->m_parent ? type::dynamic : type::static_;
}

plugin_simple::plugin_simple(const location& location, const descriptor& descriptor)
    : plugin(location, descriptor.first),
      m_elements(descriptor.second)
{
}

const elements& plugin_simple::get_elements()
{
  return m_elements;
}

std::ostream& operator<<(std::ostream& ostream, const plugin::info& info)
{
  ostream
      << "name         : " << info.m_name << std::endl
      << "description  : " << info.m_description << std::endl
      << "version      : " << info.m_version << std::endl
      << "license      : " << info.m_license << std::endl
      << "release date : " << boost::gregorian::to_simple_string(info.m_release_date);
  return ostream;
}

std::ostream& operator<<(std::ostream& ostream, const plugin::extended_info& extended_info)
{
  ostream
      << (const plugin::info&)extended_info << std::endl
      << "type         : " << (extended_info.m_type == type::dynamic ? "dynamic" : "static") << std::endl
      << "location     : " << (extended_info.m_location ? extended_info.m_location->string() : "unknown location");
  return ostream;
}

}  // namespace plugin
}  // namespace detail
}  // namespace core
}  // namespace rstream
