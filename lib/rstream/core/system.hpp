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

struct os_identity {
  std::string m_os;
  std::string m_arch;
};

std::ostream& operator<<(std::ostream& ostream, const system_info& system_info);
nlohmann::json& operator<<(nlohmann::json& json, const system_info& system_info);

system_info get_system_info();

os_identity get_compiletime_identity();

os_identity get_runtime_identity();

std::string get_compiletime_os();

std::string get_compiletime_arch();

std::string get_runtime_os();

std::string get_runtime_arch();

}  // namespace core
}  // namespace rstream
