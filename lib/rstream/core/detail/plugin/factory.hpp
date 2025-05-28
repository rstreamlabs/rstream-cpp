// See LICENSE file in the project root for license information.

#pragma once

#include <list>
#include <memory>
#include <string>

#include <boost/system/error_code.hpp>

#include "element.hpp"
#include "plugin.hpp"

namespace rstream {
namespace core {
namespace detail {
namespace plugin {

class factory {
 public:
  /// init
  factory(const config& config = default_config());

  factory(const factory&)            = delete;
  factory& operator=(const factory&) = delete;

  /// get all available plugins
  std::list<plugin::extended_info> get_plugins() const;

  /// get a specific plugin
  plugin::extended_info get_plugin(const plugin::name& name, boost::system::error_code& error_code) const;
  plugin::extended_info get_plugin(const plugin::name& name) const;

  /// get all available elements
  std::list<element::info> get_elements() const;

  /// get a specific element
  element::info get_element(const element::name& name, boost::system::error_code& error_code) const;
  element::info get_element(const element::name& name) const;

  /// instantiate an element
  element::ptr create(const element::name& name, boost::system::error_code& error_code) const;
  element::ptr create(const element::name& name) const;

  /// manually register plugin
  void register_plugin(const plugin::ptr& plugin, boost::system::error_code& error_code) const;
  void register_plugin(const plugin::ptr& plugin) const;

  /// default config
  static const config& default_config();

 private:
  class impl;

  std::shared_ptr<impl> m_impl;
};

}  // namespace plugin
}  // namespace detail
}  // namespace core
}  // namespace rstream
