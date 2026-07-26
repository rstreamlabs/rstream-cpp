// See LICENSE file in the project root for license information.

#include "version.hpp"

#include <rstream/config.hpp>

namespace rstream {
namespace core {

std::ostream& operator<<(std::ostream& ostream, const version& version)
{
  ostream << "\tmajor : " << version.m_major << std::endl;
  ostream << "\tminor : " << version.m_minor << std::endl;
  ostream << "\tpatch : " << version.m_patch;
  return ostream;
}

nlohmann::json& operator<<(nlohmann::json& json, const version& version)
{
  json.clear();
  json["major"] = version.m_major;
  json["minor"] = version.m_minor;
  json["patch"] = version.m_patch;
  return json;
}

std::ostream& operator<<(std::ostream& ostream, const compiler& compiler)
{
  ostream << "\tid      : " << compiler.m_id << std::endl;
  ostream << "\tversion : " << compiler.m_version;
  return ostream;
}

nlohmann::json& operator<<(nlohmann::json& json, const compiler& compiler)
{
  json.clear();
  json["id"]      = compiler.m_id;
  json["version"] = compiler.m_version;
  return json;
}

std::ostream& operator<<(std::ostream& ostream, const system& system)
{
  ostream << "\tsystem    : " << system.m_system << std::endl;
  ostream << "\tprocessor : " << system.m_processor;
  return ostream;
}

nlohmann::json& operator<<(nlohmann::json& json, const system& system)
{
  json.clear();
  json["system"]    = system.m_system;
  json["processor"] = system.m_processor;
  return json;
}

std::ostream& operator<<(std::ostream& ostream, const version_control& version_control)
{
  ostream << "\tbranch           : " << version_control.m_branch << std::endl;
  ostream << "\tcommit           : " << version_control.m_commit << std::endl;
  ostream << "\tunstaged_changes : " << version_control.m_unstaged_changes;
  return ostream;
}

nlohmann::json& operator<<(nlohmann::json& json, const version_control& version_control)
{
  json.clear();
  json["branch"]           = version_control.m_branch;
  json["commit"]           = version_control.m_commit;
  json["unstaged_changes"] = version_control.m_unstaged_changes;
  return json;
}

std::ostream& operator<<(std::ostream& ostream, const project_info& project_info)
{
  ostream << "version :" << std::endl
          << project_info.m_version << std::endl
          << std::endl;
  ostream << "CXX compiler :" << std::endl
          << project_info.m_compiler.m_cxx << std::endl
          << std::endl;
  ostream << "host system :" << std::endl
          << project_info.m_system.m_host << std::endl
          << std::endl;
  ostream << "target system :" << std::endl
          << project_info.m_system.m_target << std::endl
          << std::endl;
  ostream << "build date : " << project_info.m_build_date;
  if (project_info.m_version_control) {
    ostream << std::endl
            << std::endl
            << "version control :" << std::endl
            << project_info.m_version_control.get();
  }
  return ostream;
}

nlohmann::json& operator<<(nlohmann::json& json, const project_info& project_info)
{
  json.clear();
  json["version"] << project_info.m_version;
  json["compiler"]["CXX"] << project_info.m_compiler.m_cxx;
  json["system"]["host"] << project_info.m_system.m_host;
  json["system"]["target"] << project_info.m_system.m_target;
  json["build_date"] = project_info.m_build_date;
  if (project_info.m_version_control) {
    json["version_control"] << project_info.m_version_control.get();
  }
  return json;
}

project_info get_project_info()
{
  project_info project_info = {
      .m_version = {
          .m_major = RSTREAM_VERSION_MAJOR,
          .m_minor = RSTREAM_VERSION_MINOR,
          .m_patch = RSTREAM_VERSION_PATCH,
      },
      .m_compiler        = {.m_cxx = {.m_id = RSTREAM_CXX_COMPILER_ID, .m_version = RSTREAM_CXX_COMPILER_VERSION}},
      .m_system          = {.m_host = {.m_system = RSTREAM_HOST_SYSTEM, .m_processor = RSTREAM_HOST_SYSTEM_PROCESSOR}, .m_target = {.m_system = RSTREAM_TARGET_SYSTEM, .m_processor = RSTREAM_TARGET_SYSTEM_PROCESSOR}},
      .m_build_date      = RSTREAM_BUILD_DATE,
      .m_version_control = {},
  };
#if RSTREAM_GIT_FOUND
  version_control version_control = {
      .m_branch           = RSTREAM_GIT_BRANCH,
      .m_commit           = RSTREAM_GIT_COMMIT,
      .m_unstaged_changes = RSTREAM_GIT_UNSTAGED_CHANGES,
  };
  project_info.m_version_control = version_control;
#endif
  return project_info;
}

}  // namespace core
}  // namespace rstream
