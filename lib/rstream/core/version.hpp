// See LICENSE file in the project root for license information.

#pragma once

#include <ostream>
#include <string>

#include <boost/optional.hpp>

#include <nlohmann/json.hpp>

namespace rstream {
namespace core {

struct version {
  std::string m_major;
  std::string m_minor;
  std::string m_patch;
};

std::ostream& operator<<(std::ostream& ostream, const version& version);
nlohmann::json& operator<<(nlohmann::json& json, const version& version);

struct compiler {
  std::string m_id;
  std::string m_version;
};

std::ostream& operator<<(std::ostream& ostream, const compiler& compiler);
nlohmann::json& operator<<(nlohmann::json& json, const compiler& compiler);

struct system {
  std::string m_system;
  std::string m_processor;
};

std::ostream& operator<<(std::ostream& ostream, const system& system);
nlohmann::json& operator<<(nlohmann::json& json, const system& system);

struct version_control {
  std::string m_branch;
  std::string m_commit;
  bool m_unstaged_changes;
};

std::ostream& operator<<(std::ostream& ostream, const version_control& version_control);
nlohmann::json& operator<<(nlohmann::json& json, const version_control& version_control);

struct project_info {
  version m_version;
  struct {
    compiler m_cxx;
  } m_compiler;
  struct {
    system m_host;
    system m_target;
  } m_system;
  std::string m_build_date;
  boost::optional<version_control> m_version_control;
};

std::ostream& operator<<(std::ostream& ostream, const project_info& project_info);
nlohmann::json& operator<<(nlohmann::json& json, const project_info& project_info);

project_info get_project_info();

}  // namespace core
}  // namespace rstream
