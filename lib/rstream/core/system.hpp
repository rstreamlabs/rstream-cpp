// See LICENSE file in the project root for license information.

#pragma once

#include <ostream>
#include <string>

#include <nlohmann/json.hpp>

namespace rstream {
namespace core {

struct system_info {
  std::string m_sysname;
  std::string m_nodename;
  std::string m_release;
  std::string m_version;
  std::string m_machine;
};

std::ostream& operator<<(std::ostream& ostream, const system_info& system_info);
nlohmann::json& operator<<(nlohmann::json& json, const system_info& system_info);

system_info get_system_info();

}  // namespace core
}  // namespace rstream
