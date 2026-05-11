// See LICENSE file in the project root for license information.

#include "factory.hpp"

#include <map>
#include <regex>

#include <boost/dll/runtime_symbol_info.hpp>
#include <boost/filesystem.hpp>
#include <boost/system/system_error.hpp>

#include <rstream/config.hpp>
#include <rstream/core/error.hpp>
#include <rstream/core/log.hpp>

#define STR(var)                   #var
#define XSTR(var)                  STR(var)
#define RSTREAM_PLUGIN_SYMBOL_NAME XSTR(RSTREAM_PLUGIN_SYMBOL)

namespace rstream {
namespace core {
namespace detail {
namespace plugin {

class RSTREAM_GNUC_INTERNAL factory::impl {
 public:
  impl(const config& config);

  virtual ~impl();

  std::list<plugin::extended_info> get_plugins() const;

  plugin::extended_info get_plugin(const plugin::name& name, boost::system::error_code& error_code) const;

  std::list<element::info> get_elements() const;

  element::info get_element(const element::name& name, boost::system::error_code& error_code) const;

  element::ptr create(const element::name& name, boost::system::error_code& error_code) const;

  void register_plugin(const plugin::ptr& plugin, const shared_library& object, boost::system::error_code& error_code);

 private:
  using plugins = std::map<plugin::name, std::pair<plugin::ptr, shared_library>>;

  void init();

  void deinit();

  std::pair<plugins::const_iterator, elements::const_iterator> find_element(const element::name& name, boost::system::error_code& error_code) const;

  const config m_config;

  rstream::core::logger m_logger;

  plugins m_plugins;
};

factory::factory::factory(const config& config)
{
  m_impl = std::make_shared<impl>(config);
}

std::list<plugin::extended_info> factory::get_plugins() const
{
  return m_impl->get_plugins();
}

plugin::extended_info factory::get_plugin(const plugin::name& name, boost::system::error_code& error_code) const
{
  return m_impl->get_plugin(name, error_code);
}

plugin::extended_info factory::get_plugin(const plugin::name& name) const
{
  boost::system::error_code error_code;
  auto res = get_plugin(name, error_code);
  if (error_code) {
    throw boost::system::system_error(error_code);
  }
  return res;
}

std::list<element::info> factory::get_elements() const
{
  return m_impl->get_elements();
}

element::info factory::get_element(const element::name& name, boost::system::error_code& error_code) const
{
  return m_impl->get_element(name, error_code);
}

element::info factory::get_element(const element::name& name) const
{
  boost::system::error_code error_code;
  auto res = get_element(name, error_code);
  if (error_code) {
    throw boost::system::system_error(error_code);
  }
  return res;
}

element::ptr factory::create(const element::name& name, boost::system::error_code& error_code) const
{
  return m_impl->create(name, error_code);
}

element::ptr factory::create(const element::name& name) const
{
  boost::system::error_code error_code;
  auto res = create(name, error_code);
  if (error_code) {
    throw boost::system::system_error(error_code);
  }
  return res;
}

void factory::register_plugin(const plugin::ptr& plugin, boost::system::error_code& error_code) const
{
  return m_impl->register_plugin(plugin, nullptr, error_code);
}

void factory::register_plugin(const plugin::ptr& plugin) const
{
  boost::system::error_code error_code;
  register_plugin(plugin, error_code);
  if (error_code) {
    throw boost::system::system_error(error_code);
  }
}

const config& factory::default_config()
{
  static const auto default_search_path = boost::filesystem::weakly_canonical(boost::dll::program_location().parent_path().parent_path() / RSTREAM_INSTALL_PLUGINDIR);
  static const config cfg               = {
#ifdef _WIN32
      {"pattern", "^rstream-plugin(.*).dll$"},
#else
      {"pattern", "^rstream-plugin(.*).so$"},
#endif
      {"search_paths", {default_search_path.string()}},
  };
  return cfg;
}

factory::impl::impl(const config& config)
    : m_config(config),
      m_logger({"rstream", "core", "factory", fmt::format("#{}", fmt::ptr(this))})
{
#ifdef DEBUG_BUILD
  m_logger->trace("factory created");
#endif
  init();
}

factory::impl::~impl()
{
  deinit();
#ifdef DEBUG_BUILD
  m_logger->trace("factory destroyed");
#endif
}

std::list<plugin::extended_info> factory::impl::get_plugins() const
{
  std::list<plugin::extended_info> result;
  for (const auto& plugin : m_plugins) {
    result.push_back(plugin.second.first->get_extended_info());
  }
  return result;
}

plugin::extended_info factory::impl::get_plugin(const plugin::name& name, boost::system::error_code& error_code) const
{
  plugin::extended_info res = {};
  auto it                   = m_plugins.find(name);
  if (it == m_plugins.end()) {
    error_code = error::code::plugin_not_found;
  }
  else {
    res = it->second.first->get_extended_info();
  }
  return res;
}

std::list<element::info> factory::impl::get_elements() const
{
  std::list<element::info> result;
  for (const auto& plugin : m_plugins) {
    for (const auto& element : plugin.second.first->get_elements()) {
      result.push_back(element.second.m_info);
    }
  }
  return result;
}

element::info factory::impl::get_element(const element::name& name, boost::system::error_code& error_code) const
{
  element::info res = {};
  auto it           = find_element(name, error_code);
  if (!error_code) {
    res = it.second->second.m_info;
  }
  return res;
}

element::ptr factory::impl::create(const element::name& name, boost::system::error_code& error_code) const
{
  auto it = find_element(name, error_code);
  if (error_code) {
    return nullptr;
  }
  auto element = it.second->second.m_create_func();
  auto plugin  = it.first->second.first;
  auto ptr     = it.first->second.second ? element::ptr(element.get(), object_deleter(element, plugin)) : element;
  ptr->initialize(it.second->second.m_info, plugin);
  return ptr;
}

void factory::impl::register_plugin(const plugin::ptr& plugin, const shared_library& object, boost::system::error_code& error_code)
{
  auto ptr = object ? plugin::ptr(plugin.get(), object_deleter(plugin, object)) : plugin;
  ptr->initialize(m_config, object);
  ptr->init();
  m_plugins.insert(std::make_pair(plugin->get_info().m_name, std::make_pair(ptr, object)));
}

void factory::impl::init()
{
  auto pattern      = m_config.find("pattern");
  auto search_paths = m_config.find("search_paths");
  if (pattern == m_config.end()
      || search_paths == m_config.end()) {
    return;
  }
  std::list<boost::filesystem::path> plugins;
  const std::regex regex(pattern->get<std::string>(), std::regex_constants::ECMAScript);
  for (const auto& search_path : *search_paths) {
    boost::filesystem::path path(search_path.get<std::string>());
    if (!boost::filesystem::is_directory(path)) {
      continue;
    }
    boost::filesystem::directory_iterator end_itr;
    for (boost::filesystem::directory_iterator itr(path); itr != end_itr; ++itr) {
      if (!boost::filesystem::is_regular_file(itr->path())) {
        continue;
      }
      if (std::regex_search(itr->path().filename().string(), regex)) {
        plugins.push_back(itr->path());
      }
    }
  }
#ifdef DEBUG_BUILD
  struct RSTREAM_GNUC_INTERNAL shared_library_deleter {
    void operator()(boost::dll::shared_library* ptr)
    {
      boost::filesystem::path location = ptr->location();
      std::default_delete<boost::dll::shared_library>()(ptr);
      rstream::core::default_logger()->trace("shared library '{}' unloaded", location.string());
    }
  };
#endif
  for (const auto& path : plugins) {
#ifdef DEBUG_BUILD
    auto library = std::shared_ptr<boost::dll::shared_library>(new boost::dll::shared_library(path, boost::dll::load_mode::rtld_lazy), shared_library_deleter());
#else
    auto library = std::make_shared<boost::dll::shared_library>(path, boost::dll::load_mode::rtld_lazy);
#endif
#ifdef DEBUG_BUILD
    rstream::core::default_logger()->trace("shared library '{}' loaded", library->location().string());
#endif
    auto plugin = library->get_alias<plugin::ptr()>(RSTREAM_PLUGIN_SYMBOL_NAME)();
    boost::system::error_code error_code;
    register_plugin(plugin, library, error_code);
    if (error_code) {
      rstream::core::default_logger()->warn("failed to register plugin [error_code: {}]", error_code.message());
    }
  }
}

void factory::impl::deinit()
{
  m_plugins.clear();
}

std::pair<factory::impl::plugins::const_iterator, elements::const_iterator> factory::impl::find_element(const element::name& name, boost::system::error_code& error_code) const
{
  for (auto it_1 = m_plugins.begin(); it_1 != m_plugins.end(); ++it_1) {
    const auto& elements = it_1->second.first->get_elements();
    auto it_2            = elements.find(name);
    if (it_2 != elements.end()) {
      return std::make_pair(it_1, it_2);
    }
  }
  error_code = error::code::plugin_not_found;
  return std::pair<factory::impl::plugins::const_iterator, elements::const_iterator>();
}

}  // namespace plugin
}  // namespace detail
}  // namespace core
}  // namespace rstream
