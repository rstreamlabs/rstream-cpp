// See LICENSE file in the project root for license information.
#pragma once

#include <filesystem>

namespace rstream::tunnel::cli {

inline std::filesystem::path packaged_terminfo(const std::filesystem::path& executable)
{
  const auto share = executable.parent_path().parent_path() / "share";
  std::error_code error;
  const auto directory = share / "terminfo";
  if (std::filesystem::is_directory(directory, error)) {
    return directory;
  }
  const auto database = share / "terminfo.db";
  if (std::filesystem::is_regular_file(database, error)) {
    return database;
  }
  return {};
}

}  // namespace rstream::tunnel::cli
