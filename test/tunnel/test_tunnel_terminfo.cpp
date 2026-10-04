// See LICENSE file in the project root for license information.
#include <cassert>
#include <filesystem>
#include <fstream>
#include <random>

#include "terminfo.hpp"

int main()
{
  namespace fs = std::filesystem;
  std::random_device random;
  const auto root = fs::temp_directory_path() / ("rstream-terminfo-" + std::to_string(random()) + "-" + std::to_string(random()));
  assert(fs::create_directory(root));
  struct cleanup {
    fs::path path;
    ~cleanup()
    {
      std::error_code error;
      fs::remove_all(path, error);
    }
  } guard{root};
  const auto executable = root / "bin" / "rstream-tunnel";
  const auto database   = root / "share" / "terminfo.db";
  const auto directory  = root / "share" / "terminfo";
  using rstream::tunnel::cli::packaged_terminfo;
  assert(packaged_terminfo(executable).empty());
  fs::create_directories(root / "share");
  std::ofstream(database) << "legacy database fixture";
  assert(packaged_terminfo(executable) == database);
  fs::create_directories(directory / "x");
  std::ofstream(directory / "x" / "xterm-256color") << "directory fixture";
  assert(packaged_terminfo(executable) == directory);
  fs::remove(database);
  assert(packaged_terminfo(executable) == directory);
  fs::remove_all(directory);
  std::ofstream(directory) << "not a directory";
  assert(packaged_terminfo(executable).empty());
}
