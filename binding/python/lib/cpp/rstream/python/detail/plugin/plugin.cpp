// See LICENSE file in the project root for license information.

#include "plugin.hpp"

#include <regex>

#include <boost/filesystem.hpp>

#include <rstream/core/log.hpp>
#include <rstream/python/exception.hpp>

#include "element.hpp"

namespace rstream {
namespace python {
namespace detail {
namespace plugin {

template <typename T>
static void set_key_value_if_undefined(rstream::core::plugin::config& dst, const rstream::core::plugin::config* src, const std::string& key, const T& value);

plugin::plugin(const rstream::core::plugin::plugin::location& location, const rstream::core::plugin::plugin::info& info)
    : rstream::core::plugin::plugin(location, info)
{
  if (!Py_IsInitialized()) {
    Py_Initialize();
  }
}

plugin::~plugin()
{
  m_elements.clear();
  if (Py_IsInitialized()) {
    Py_FinalizeEx();
  }
}

void plugin::register_module(const std::string& filename)
{
  try {
    register_module_internal(filename);
  }
  catch (const boost::python::error_already_set&) {
    throw rstream::python::exception();
  }
}

const rstream::core::detail::plugin::elements& plugin::get_elements()
{
  return m_elements;
}

void plugin::init()
{
  m_config = get_default_config(get_config());
  std::list<boost::filesystem::path> modules;
  const std::regex regex(m_config["pattern"].get<std::string>(), std::regex_constants::ECMAScript);
  auto search = [&modules, &regex](const std::string& search_path) {
    boost::filesystem::path path(search_path);
    if (!boost::filesystem::is_directory(path)) {
      return;
    }
    boost::filesystem::directory_iterator end_itr;
    for (boost::filesystem::directory_iterator itr(path); itr != end_itr; ++itr) {
      if (!boost::filesystem::is_regular_file(itr->path())) {
        continue;
      }
      if (std::regex_search(itr->path().filename().string(), regex)) {
        modules.push_back(itr->path());
      }
    }
  };
  for (const auto& search_path : m_config["search_paths"]) {
    search((boost::filesystem::path(search_path.get<std::string>()) / m_config["suffix"].get<std::string>()).string());
  }
  for (const auto& search_path : m_config["extra_search_paths"]) {
    search(search_path.get<std::string>());
  }
  for (const auto& module : modules) {
    register_module(module.string());
  }
}

void plugin::register_module_internal(const std::string& filename)
{
  auto module = import_module_file(filename.c_str());
  if (!module) {
    return;
  }
  boost::python::list elements(module.attr("get_elements")());
  for (boost::python::ssize_t i = 0; i < boost::python::len(elements); ++i) {
    auto py_info                              = elements[i].attr("get_info")();
    rstream::core::plugin::element::info info = {
        .m_name        = boost::python::extract<std::string>(py_info.attr("name")),
        .m_description = boost::python::extract<std::string>(py_info.attr("description")),
    };
    rstream::core::plugin::element::create_func func = [var = elements[i]]() {
      return std::make_shared<element>(var());
    };
    rstream::core::plugin::element::handle handle = {
        .m_info        = info,
        .m_create_func = func,
    };
    m_elements.insert(std::make_pair(info.m_name, handle));
  }
}

rstream::core::plugin::config plugin::get_default_config(const rstream::core::plugin::config& base)
{
  rstream::core::plugin::config dst;
  set_key_value_if_undefined<rstream::core::plugin::config>(dst, &base, "search_paths", {});
  auto it  = base.find("python");
  auto src = it != base.end() ? &it.value() : nullptr;
  set_key_value_if_undefined<rstream::core::plugin::config>(dst, src, "extra_search_paths", {});
  set_key_value_if_undefined(dst, src, "pattern", "^(rstream-)?plugin(.*).py$");
  set_key_value_if_undefined(dst, src, "suffix", "python");
  return dst;
}

template <typename T>
void set_key_value_if_undefined(rstream::core::plugin::config& dst, const rstream::core::plugin::config* src, const std::string& key, const T& value)
{
  if (src) {
    auto it = src->find(key);
    if (it != src->end()) {
      dst[key] = *it;
      return;
    }
  }
  dst[key] = value;
};

}  // namespace plugin
}  // namespace detail
}  // namespace python
}  // namespace rstream
