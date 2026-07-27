#!/usr/bin/env bash

set -e

if [ -z "${BASH_VERSINFO:-}" ] || [ "${BASH_VERSINFO[0]}" -lt 4 ]; then
  echo "Bash 4 or later is required. On macOS, install Homebrew bash and ensure it is first in PATH." >&2
  exit 1
fi

script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)

# Default values
default_build_type="Release"
default_enable_strict_warnings="on"
default_compose_file="${script_dir}/conan/compose.yaml"
default_export_package_name="rstream-utils"
default_linux_archs=("x86_i686" "x86_core2" "x86_64" "x86_64_v2" "x86_64_v3" "x86_64_v4" "armv6" "armv6hf" "armv7" "armv7hf" "arm64" "mips" "mipsle" "mips64" "mips64le" "riscv64")
default_linux_build_shared=("on" "off")
default_linux_plugin_modes=("auto")
default_linux_tclibcs=("glibc" "musl")
default_linux_toolchain_version="5.0.2"
default_linux_toolchain="yocto-toolchain"
default_macos_archs=("x86_64" "x86_64_v2" "x86_64_v3" "x86_64_v4" "arm64" "apple-m1" "apple-m2")
default_macos_build_shared=("on" "off")
default_macos_plugin_modes=("auto")
default_oss=("macos" "linux" "windows")
default_use_docker="on"
default_windows_archs=("x86_i686" "x86_core2" "x86_64" "x86_64_v2" "x86_64_v3" "x86_64_v4" "arm64")
default_windows_build_shared=("on" "off")
default_windows_plugin_modes=("auto")
default_use_patched_conan_deps="on"
default_warnings_as_errors="on"
default_patched_conan_channel="conan/stable"
default_patched_boost_version="1.85.0"
default_patched_ncurses_version="6.5"
docker_conan_config_synced="off"
patched_conan_recipes_exported="off"
package_name_cache=""
package_version_cache=""

function resolve_list {
  local value="$1"
  # shellcheck disable=SC2034
  local -n dst="$2"
  shift 2
  if [ -n "${value}" ]; then
    read -r -a dst <<<"${value}"
  else
    # shellcheck disable=SC2034
    dst=("$@")
  fi
}

function inspect_package_metadata {
  if [ -n "${package_name_cache}" ] && [ -n "${package_version_cache}" ]; then
    return
  fi
  local metadata
  metadata=$(conan inspect "${script_dir}" --format=json | python3 -c 'import json, sys; data = json.load(sys.stdin); print(data["name"] + "\t" + data["version"])')
  package_name_cache="${metadata%%$'\t'*}"
  package_version_cache="${metadata#*$'\t'}"
}

function docker_run_builder {
  docker compose -f "${docker_compose_file}" run --rm --no-deps "$@"
}

# Allow overrides from environment variables
build_type="${BUILD_TYPE:-${default_build_type}}"
enable_strict_warnings="${ENABLE_STRICT_WARNINGS:-${default_enable_strict_warnings}}"
docker_compose_file="${DOCKER_COMPOSE_FILE-${default_compose_file}}"
export_package_name="${EXPORT_PACKAGE_NAME:-${default_export_package_name}}"
linux_archs=()
linux_build_shared=()
linux_plugin_modes=()
linux_tclibcs=()
macos_archs=()
macos_build_shared=()
macos_plugin_modes=()
oss=()
windows_archs=()
windows_build_shared=()
windows_plugin_modes=()
resolve_list "${LINUX_ARCHS:-}" linux_archs "${default_linux_archs[@]}"
resolve_list "${LINUX_BUILD_SHARED:-}" linux_build_shared "${default_linux_build_shared[@]}"
resolve_list "${LINUX_PLUGIN_MODES:-}" linux_plugin_modes "${default_linux_plugin_modes[@]}"
resolve_list "${LINUX_TCLIBCS:-}" linux_tclibcs "${default_linux_tclibcs[@]}"
linux_toolchain_version="${LINUX_TOOLCHAIN_VERSION:-${default_linux_toolchain_version}}"
linux_toolchain="${LINUX_TOOLCHAIN:-${default_linux_toolchain}}"
resolve_list "${MACOS_ARCHS:-}" macos_archs "${default_macos_archs[@]}"
resolve_list "${MACOS_BUILD_SHARED:-}" macos_build_shared "${default_macos_build_shared[@]}"
resolve_list "${MACOS_PLUGIN_MODES:-}" macos_plugin_modes "${default_macos_plugin_modes[@]}"
resolve_list "${OSS:-}" oss "${default_oss[@]}"
use_docker="${USE_DOCKER:-${default_use_docker}}"
resolve_list "${WINDOWS_ARCHS:-}" windows_archs "${default_windows_archs[@]}"
resolve_list "${WINDOWS_BUILD_SHARED:-}" windows_build_shared "${default_windows_build_shared[@]}"
resolve_list "${WINDOWS_PLUGIN_MODES:-}" windows_plugin_modes "${default_windows_plugin_modes[@]}"
use_patched_conan_deps="${USE_PATCHED_CONAN_DEPS:-${default_use_patched_conan_deps}}"
warnings_as_errors="${WARNINGS_AS_ERRORS:-${default_warnings_as_errors}}"
patched_conan_channel="${PATCHED_CONAN_CHANNEL:-${default_patched_conan_channel}}"
patched_boost_version="${PATCHED_BOOST_VERSION:-${default_patched_boost_version}}"
patched_ncurses_version="${PATCHED_NCURSES_VERSION:-${default_patched_ncurses_version}}"
if [[ "${patched_conan_channel}" != */* ]]; then
  echo "PATCHED_CONAN_CHANNEL must use the user/channel form." >&2
  exit 1
fi

blacklist=()
ARCH="${ARCH:-}"
BUILD_SHARED="${BUILD_SHARED:-}"
CMD="${CMD:-}"
LIBC="${LIBC:-}"
OS="${OS:-}"
PLUGIN_MODE="${PLUGIN_MODE:-}"
SRC_PATH="${SRC_PATH:-}"

function call_os {
  local suffix="$1"
  local function_name="${OS}_${suffix}"
  shift
  "${function_name}" "$@"
}

function is_blacklisted {
  local candidate="$1"
  local item
  for item in "${blacklist[@]}"; do
    if [ "${item}" = "${candidate}" ]; then
      return 0
    fi
  done
  return 1
}

glibc_version="2.39"
macosx_version_min="11.0"
musl_version="1.2.4"
windows_ntddi_version="0x0A000006"
windows_win32_winnt="0x0A00"

declare -A extra_env_vars
extra_env_vars["linux-mips64-glibc"]="CONAN_OPENSSL_CONFIGURATION=linux64-mips64"
extra_env_vars["linux-mips64-musl"]="CONAN_OPENSSL_CONFIGURATION=linux64-mips64"
extra_env_vars["linux-mips64le-glibc"]="CONAN_OPENSSL_CONFIGURATION=linux64-mips64"
extra_env_vars["linux-mips64le-musl"]="CONAN_OPENSSL_CONFIGURATION=linux64-mips64"
extra_env_vars["linux-riscv64-glibc"]="CONAN_OPENSSL_CONFIGURATION=linux64-riscv64"
extra_env_vars["linux-riscv64-musl"]="CONAN_OPENSSL_CONFIGURATION=linux64-riscv64"
extra_env_vars["windows-x86_64"]="CONAN_OPENSSL_CONFIGURATION=mingw64"
extra_env_vars["windows-x86_64_v2"]="CONAN_OPENSSL_CONFIGURATION=mingw64"
extra_env_vars["windows-x86_64_v3"]="CONAN_OPENSSL_CONFIGURATION=mingw64"
extra_env_vars["windows-x86_64_v4"]="CONAN_OPENSSL_CONFIGURATION=mingw64"
extra_env_vars["windows-x86_i686"]="CONAN_OPENSSL_CONFIGURATION=mingw"
extra_env_vars["windows-x86_core2"]="CONAN_OPENSSL_CONFIGURATION=mingw"

declare -A extra_conan_options
extra_conan_options["linux-ppc64-glibc"]="--options boost/*:without_charconv=True"
extra_conan_options["linux-ppc64-musl"]="--options boost/*:without_charconv=True"
extra_conan_options["linux-ppc64le-glibc"]="--options boost/*:without_charconv=True"
extra_conan_options["linux-ppc64le-musl"]="--options boost/*:without_charconv=True"
extra_conan_options["linux-riscv64-glibc"]="--options openssl/*:no_asm=True"
extra_conan_options["linux-riscv64-musl"]="--options openssl/*:no_asm=True"

function patched_conan_conf {
  if [ "${use_patched_conan_deps}" != "on" ]; then
    return
  fi
  local opts=("-o" "rstream/*:boost_ref=boost/${patched_boost_version}@${patched_conan_channel}")
  if [ "${OS}" != "windows" ] && [ -n "${patched_ncurses_version}" ]; then
    opts+=("-o" "rstream/*:ncurses_ref=ncurses/${patched_ncurses_version}@${patched_conan_channel}")
  fi
  echo "${opts[@]}"
}

function resolve_channel {
  if [ -n "${CHANNEL:-}" ]; then
    echo "${CHANNEL}"
  elif [ "${RSTREAM_URL:-}" = "https://rstream.io" ]; then
    echo "stable"
  else
    echo "dev"
  fi
}

function shared {
  if [ "${BUILD_SHARED}" = "on" ]; then
    echo "True"
  else
    echo "False"
  fi
}

function conan_bool {
  case "$1" in
  on)
    echo "True"
    ;;
  off)
    echo "False"
    ;;
  *)
    echo "Unsupported boolean value '$1'. Expected on or off." >&2
    return 1
    ;;
  esac
}

function package_name {
  inspect_package_metadata
  echo "${package_name_cache}"
}

function package_version {
  inspect_package_metadata
  echo "${package_version_cache}"
}

function conan_arch {
  if [[ "${ARCH}" == x86_64* ]]; then
    echo "x86_64"
  elif [[ "${ARCH}" == x86* ]]; then
    echo "x86"
  elif [[ "${ARCH}" == mips64* ]]; then
    echo "mips64"
  elif [[ "${ARCH}" == mips* ]]; then
    echo "mips"
  elif [[ "${ARCH}" == arm64* ]]; then
    echo "armv8"
  elif [[ "${ARCH}" == armv6* ]]; then
    echo "armv6"
  else
    echo "${ARCH}"
  fi
}

function linux_static_libstdcxx {
  if [ "${LIBC}" = "musl" ] && [ "${BUILD_SHARED}" != "on" ] && [ "${PLUGIN_MODE}" != "dynamic" ]; then
    echo "True"
  else
    echo "False"
  fi
}

function windows_static_libstdcxx {
  if [ "${BUILD_SHARED}" != "on" ] && [ "${PLUGIN_MODE}" != "dynamic" ]; then
    echo "True"
  else
    echo "False"
  fi
}

function windows_toolchain_target {
  if [[ "${ARCH}" == x86_64* ]]; then
    echo "x86_64-w64-mingw32"
  elif [[ "${ARCH}" == x86* ]]; then
    echo "i686-w64-mingw32"
  elif [[ "${ARCH}" == arm64* ]]; then
    echo "aarch64-w64-mingw32"
  else
    echo "Unsupported Windows architecture '${ARCH}'." >&2
    return 1
  fi
}

function static_plugins_option {
  case "${PLUGIN_MODE}" in
  auto)
    ;;
  static)
    echo "--options $(package_name)/*:static_plugins=True"
    ;;
  dynamic)
    echo "--options $(package_name)/*:static_plugins=False"
    ;;
  *)
    echo "Unsupported plugin mode '${PLUGIN_MODE}'. Expected auto, static, or dynamic." >&2
    return 1
    ;;
  esac
}

function plugin_mode_path {
  if [ "${PLUGIN_MODE}" != "auto" ]; then
    echo "/plugins-${PLUGIN_MODE}"
  fi
}

function package_options {
  echo "--options $(package_name)/*:shared=$(shared) $(static_plugins_option) --options $(package_name)/*:enable_strict_warnings=$(conan_bool "${enable_strict_warnings}") --options $(package_name)/*:warnings_as_errors=$(conan_bool "${warnings_as_errors}")"
}

function linux_package_options {
  echo "$(package_options) --options $(package_name)/*:static_libstdcxx=$(call_os static_libstdcxx)"
}

function macos_package_options {
  package_options
}

function windows_package_options {
  echo "$(package_options) --options $(package_name)/*:static_libstdcxx=$(call_os static_libstdcxx)"
}

function linux_conan_extra_options {
  echo "${extra_conan_options["${OS}-${ARCH}-${LIBC}"]}"
}

function macos_conan_extra_options {
  echo "${extra_conan_options["${OS}-${ARCH}"]}"
}

function windows_conan_extra_options {
  echo "${extra_conan_options["${OS}-${ARCH}"]} --options openssl/*:no_apps=True"
}

function linux_conan_extra_settings {
  echo ""
}

function macos_conan_extra_settings {
  echo ""
}

function windows_conan_extra_settings {
  echo "-s:h openssl/*:compiler=gcc -s:h openssl/*:compiler.version=13 -s:h openssl/*:compiler.libcxx=libstdc++11 -s:h openssl/*:compiler.threads=posix -s:h protobuf/*:compiler.runtime=dynamic -s:h protobuf/*:compiler.runtime_type=${build_type} -s:h zlib/*:compiler=gcc -s:h zlib/*:compiler.version=13 -s:h zlib/*:compiler.libcxx=libstdc++11 -s:h zlib/*:compiler.threads=posix"
}

function conan_options {
  echo "$(call_os conan_extra_options) $(call_os conan_extra_settings) $(call_os package_options) --profile:build=default --settings:host build_type=${build_type}"
}

function linux_conan_options {
  echo "$(conan_options) --profile:host=${linux_toolchain} --settings:host arch=$(conan_arch) --settings:host os.sdk=${linux_toolchain}-${linux_toolchain_version}-${ARCH}-${LIBC} --options:build ${linux_toolchain}/${linux_toolchain_version}:arch=${ARCH} --options:build ${linux_toolchain}/${linux_toolchain_version}:libc=${LIBC}"
}

function macos_conan_options {
  echo "$(conan_options) --profile:host=${OS}-${ARCH}"
}

function windows_conan_options {
  echo "$(conan_options) --profile:host=${OS}-${ARCH}"
}

function linux_package_outdir {
  echo "out/release/$(package_version)/${OS}/${ARCH}/${LIBC}/$([ "${BUILD_SHARED}" == "on" ] && echo "shared" || echo "static")$(plugin_mode_path)"
}

function macos_package_outdir {
  echo "out/release/$(package_version)/${OS}/${ARCH}/$([ "${BUILD_SHARED}" == "on" ] && echo "shared" || echo "static")$(plugin_mode_path)"
}

function windows_package_outdir {
  echo "out/release/$(package_version)/${OS}/${ARCH}/$([ "${BUILD_SHARED}" == "on" ] && echo "shared" || echo "static")$(plugin_mode_path)"
}

function cmd_build {
  echo "conan create $(call_os conan_options) --build=$(package_name) --build=missing -o rstream/*:build_channel=$(resolve_channel) -o rstream/*:build_os=${OS} -o rstream/*:build_arch=${ARCH} $(patched_conan_conf) ${SRC_PATH}"
}

function linux_cmd_build {
  echo "${extra_env_vars["${OS}-${ARCH}-${LIBC}"]} $(cmd_build)"
}

function macos_cmd_build {
  echo "${extra_env_vars["${OS}-${ARCH}"]} $(cmd_build)"
}

function windows_cmd_build {
  echo "${extra_env_vars["${OS}-${ARCH}"]} $(cmd_build)"
}

function cmd_export {
  echo "EXPORT_PACKAGE_NAME=${export_package_name} conan install $(call_os conan_options) --requires $(package_name)/$(package_version) --deployer ${SRC_PATH}/deploy.py -of ${SRC_PATH}/$(call_os package_outdir) -o rstream/*:build_channel=$(resolve_channel) -o rstream/*:build_os=${OS} -o rstream/*:build_arch=${ARCH} $(patched_conan_conf)"
}

function linux_cmd_export {
  echo "${extra_env_vars["${OS}-${ARCH}-${LIBC}"]} $(cmd_export)"
}

function macos_cmd_export {
  echo "${extra_env_vars["${OS}-${ARCH}"]} $(cmd_export)"
}

function windows_cmd_export {
  local toolchain_target
  toolchain_target=$(windows_toolchain_target)
  echo "${extra_env_vars["${OS}-${ARCH}"]} RSTREAM_WINDOWS_OBJDUMP=/opt/llvm-mingw/bin/${toolchain_target}-objdump RSTREAM_WINDOWS_RUNTIME_DIR=/opt/llvm-mingw/${toolchain_target}/bin $(cmd_export)"
}

function linux_run_docker {
  docker_run_builder --entrypoint "bash" -v "${script_dir}:/source:rw" -e CHANNEL -e VERSION -e OS -e ARCH -e LIBC -e BUILD_SHARED -e PLUGIN_MODE -e SRC_PATH "${@:2}" conan2-builder -c "$1"
}

function windows_run_docker {
  docker_run_builder --entrypoint "bash" -v "${script_dir}:/source:rw" -e CHANNEL -e VERSION -e OS -e ARCH -e BUILD_SHARED -e PLUGIN_MODE -e SRC_PATH "${@:2}" conan2-builder -c "$1"
}

function sync_docker_conan_config {
  if [ "${docker_conan_config_synced}" = "on" ]; then
    return
  fi
  DOCKER_COMPOSE_FILE="${docker_compose_file}" "${script_dir}/conan/update-conan-profile.sh"
  docker_conan_config_synced="on"
}

function export_patched_conan_recipes {
  if [ "${use_patched_conan_deps}" != "on" ] || [ "${patched_conan_recipes_exported}" = "on" ]; then
    return
  fi
  local export_local="off"
  local export_docker="off"
  local os
  local recipe
  for os in "${oss[@]}"; do
    if [ "${os}" = "macos" ] || [ "${use_docker}" != "on" ]; then
      export_local="on"
    elif [ "${os}" = "linux" ] || [ "${os}" = "windows" ]; then
      export_docker="on"
    fi
  done
  if [ "${export_local}" = "on" ]; then
    for recipe in boost ncurses; do
      (
        cd "${script_dir}/conan/recipes/${recipe}"
        python3 export.py
      ) || exit 1
    done
  fi
  if [ "${export_docker}" = "on" ]; then
    docker_run_builder --entrypoint "bash" -v "${script_dir}:/source:rw" conan2-builder -c \
      "set -e; for recipe in boost ncurses; do cd /source/conan/recipes/\${recipe} && python3 export.py; done" || exit 1
  fi
  patched_conan_recipes_exported="on"
}

function linux_run_build {
  if [ "${use_docker}" = "on" ]; then
    export SRC_PATH=/source
    call_os run_docker "$(call_os cmd_build)"
  else
    export SRC_PATH=${script_dir}
    bash -c "$(call_os cmd_build)"
  fi
}

function macos_run_build {
  unset CC
  unset CFLAGS
  unset CMAKE_PREFIX_PATH
  unset CPPFLAGS
  unset CXX
  unset CXXFLAGS
  unset LDFLAGS
  unset PKG_CONFIG_PATH
  export SRC_PATH=${script_dir}
  bash -c "$(call_os cmd_build)"
}

function windows_run_build {
  if [ "${use_docker}" = "on" ]; then
    export SRC_PATH=/source
    call_os run_docker "$(call_os cmd_build)"
  else
    export SRC_PATH=${script_dir}
    bash -c "$(call_os cmd_build)"
  fi
}

function linux_run_export {
  if [ "${use_docker}" = "on" ]; then
    export SRC_PATH=/source
    call_os run_docker "$(call_os cmd_export) && chown -R $(id -u):$(id -g) /source/$(call_os package_outdir)"
  else
    export SRC_PATH=${script_dir}
    bash -c "$(call_os cmd_export)"
  fi
}

function macos_run_export {
  export SRC_PATH=${script_dir}
  bash -c "$(call_os cmd_export)"
}

function windows_run_export {
  if [ "${use_docker}" = "on" ]; then
    export SRC_PATH=/source
    call_os run_docker "$(call_os cmd_export) && chown -R $(id -u):$(id -g) /source/$(call_os package_outdir)"
  else
    export SRC_PATH=${script_dir}
    bash -c "$(call_os cmd_export)"
  fi
}

function linux_get_outdir {
  call_os package_outdir
}

function macos_get_outdir {
  call_os package_outdir
}

function windows_get_outdir {
  call_os package_outdir
}

function linux_get_version {
  package_version
}

function macos_get_version {
  package_version
}

function windows_get_version {
  package_version
}

function run_upload {
  export SRC_PATH=${script_dir}
  if [ -f .env.local ]; then
    # shellcheck source=/dev/null
    source .env.local
  fi
  outdir=$(call_os get_outdir)
  extension=$([ "${OS}" = "windows" ] && echo ".zip" || echo ".tar.gz")
  archive="${outdir}/packages/${export_package_name}${extension}"
  name="${export_package_name}"
  version=$(call_os get_version)
  channel=$(resolve_channel)
  shared=$([ "${BUILD_SHARED}" = "on" ] && echo "true" || echo "false")
  plugin_suffix=$([ "${PLUGIN_MODE}" = "auto" ] && echo "" || echo "-plugins-${PLUGIN_MODE}")
  if [ "${OS}" = "linux" ]; then
    filename="${export_package_name}-${version}-${OS}-${ARCH}-${LIBC}-$([ "${BUILD_SHARED}" == "on" ] && echo "shared" || echo "static")${plugin_suffix}${extension}"
  else
    filename="${export_package_name}-${version}-${OS}-${ARCH}-$([ "${BUILD_SHARED}" == "on" ] && echo "shared" || echo "static")${plugin_suffix}${extension}"
  fi
  checksum=$(shasum -a 256 "${archive}" | awk '{print $1}')
  if [ "${OS}" = "linux" ]; then
    metadata="{\"libc.vendor\": \"${LIBC}\", \"libc.version\": \"$([ "${LIBC}" == "glibc" ] && echo "${glibc_version}" || echo "${musl_version}")\"}"
  elif [ "${OS}" = "macos" ]; then
    metadata="{\"macosx.version.min\": \"${macosx_version_min}\"}"
  elif [ "${OS}" = "windows" ]; then
    metadata="{\"windows.win32_winnt\": \"${windows_win32_winnt}\", \"windows.ntddi_version\": \"${windows_ntddi_version}\"}"
  fi
  echo "Uploading '${filename}'..."
  url="${RSTREAM_URL}/api/packages?name=${name}&version=${version}&channel=${channel}&os=${OS}&arch=${ARCH}&shared=${shared}&filename=${filename}&checksum=${checksum}&metadata=$(echo "${metadata}" | jq -s -R -r @uri)"
  if [ "${OS}" = "linux" ]; then
    url="${url}&libc=${LIBC}"
  fi
  response=$(curl --fail -H "Authorization: Bearer ${RSTREAM_TOKEN}" -i -s -S -X PUT "${url}")
  package_id=$(echo "${response}" | grep 'x-package-id' | cut -d ' ' -f2 | tr -d '\r')
  signed_url=$(echo "${response}" | grep 'location:' | cut -d ' ' -f2 | tr -d '\r')
  curl --progress-bar --upload-file "${archive}" --fail -H "Content-Type: application/octet-stream" -X PUT "${signed_url}" | cat
  printf 'name:%s\nid:%s\nversion:%s\nchannel:%s\nos:%s\narch:%s\nshared:%s\nfilename:%s\nchecksum:%s\n' \
    "${name}" "${package_id}" "${version}" "${channel}" "${OS}" "${ARCH}" "${shared}" "${filename}" "${checksum}" >"${archive}.info"
  if [ "${OS}" = "linux" ]; then
    printf 'libc:%s\n' "${LIBC}" >>"${archive}.info"
  fi
  echo "Package uploaded: ${package_id}"
}

function linux_run_upload {
  run_upload
}

function macos_run_upload {
  run_upload
}

function windows_run_upload {
  run_upload
}

function linux_run {
  if [ "${use_docker}" != "on" ] && [ "$(uname -s)" != "Linux" ]; then
    echo "Linux build must be run on Linux or with docker"
    exit 1
  fi
  for arch in "${linux_archs[@]}"; do
    for libc in "${linux_tclibcs[@]}"; do
      config="${OS}-${arch}-${libc}"
      if is_blacklisted "${config}"; then
        echo "Skipping blacklisted configuration: ${config}"
        continue
      fi
      for build_shared in "${linux_build_shared[@]}"; do
        for plugin_mode in "${linux_plugin_modes[@]}"; do
          echo "Running ${CMD}: os=${OS} arch=${arch} libc=${libc} shared=${build_shared} plugins=${plugin_mode}"
          if ! ARCH=${arch} LIBC=${libc} BUILD_SHARED=${build_shared} PLUGIN_MODE=${plugin_mode} call_os "run_${CMD}"; then
            echo "Failed to ${CMD} for ${config} with shared libraries ${build_shared} and ${plugin_mode} plugins"
            exit 1
          fi
        done
      done
    done
  done
}

function macos_run {
  if [ "$(uname -s)" != "Darwin" ]; then
    echo "macOS build must be run on macOS"
    exit 1
  fi
  for arch in "${macos_archs[@]}"; do
    config="${OS}-${arch}"
    if is_blacklisted "${config}"; then
      echo "Skipping blacklisted configuration: ${config}"
      continue
    fi
    for build_shared in "${macos_build_shared[@]}"; do
      for plugin_mode in "${macos_plugin_modes[@]}"; do
        echo "Running ${CMD}: os=${OS} arch=${arch} shared=${build_shared} plugins=${plugin_mode}"
        if ! ARCH=${arch} BUILD_SHARED=${build_shared} PLUGIN_MODE=${plugin_mode} call_os "run_${CMD}"; then
          echo "Failed to ${CMD} for ${config} with shared libraries ${build_shared} and ${plugin_mode} plugins"
          exit 1
        fi
      done
    done
  done
}

function windows_run {
  if [ "${use_docker}" != "on" ] && [ "$(uname -s)" != "Linux" ]; then
    echo "Windows build must be run on Linux or with docker"
    exit 1
  fi
  for arch in "${windows_archs[@]}"; do
    config="${OS}-${arch}"
    if is_blacklisted "${config}"; then
      echo "Skipping blacklisted configuration: ${config}"
      continue
    fi
    for build_shared in "${windows_build_shared[@]}"; do
      for plugin_mode in "${windows_plugin_modes[@]}"; do
        echo "Running ${CMD}: os=${OS} arch=${arch} shared=${build_shared} plugins=${plugin_mode}"
        if ! ARCH=${arch} BUILD_SHARED=${build_shared} PLUGIN_MODE=${plugin_mode} call_os "run_${CMD}"; then
          echo "Failed to ${CMD} for ${config} with shared libraries ${build_shared} and ${plugin_mode} plugins"
          exit 1
        fi
      done
    done
  done
}

function run {
  # we require conan 2.0.0 or later
  conan_version=$(conan --version | cut -d ' ' -f3)
  if [ "$(printf '%s\n' "2.0.0" "${conan_version}" | sort -V | head -n1)" != "2.0.0" ]; then
    echo "Conan 2.0.0 or later is required"
    exit 1
  fi
  inspect_package_metadata
  if [ "${use_docker}" = "on" ]; then
    for os in "${oss[@]}"; do
      if [ "${os}" = "linux" ] || [ "${os}" = "windows" ]; then
        sync_docker_conan_config
        break
      fi
    done
  fi
  export_patched_conan_recipes
  for os in "${oss[@]}"; do
    OS=${os} call_os run
  done
}

function show_help {
  echo "rstream Conan cross-platform builder"
  echo ""
  echo "Usage: $0 [build|export|upload|help]"
  echo ""
  echo "  build             : Build and validate the configured target matrix."
  echo "  export            : Export the build output to the out directory."
  echo "  upload            : Upload the exported build output."
  echo "  help, -h, --help  : Show this help message."
  echo ""
  echo "Environment Variables:"
  echo ""
  echo "  BUILD_TYPE             : Set the CMake build type (default: ${default_build_type})."
  echo "  DOCKER_COMPOSE_FILE     : Set the docker-compose file to use (linux)."
  echo "  CONAN_DOCKER_IMAGE      : Set the Conan builder image name."
  echo "  CONAN_DOCKER_PLATFORM   : Set its host platform (default: linux/amd64)."
  echo "  ENABLE_STRICT_WARNINGS  : Enable strict project warnings (default: ${default_enable_strict_warnings})."
  echo "  EXPORT_PACKAGE_NAME     : Set the package name to export."
  echo "  LINUX_ARCHS             : Set the machine architecture to build for (linux)."
  echo "  LINUX_BUILD_SHARED      : Build shared or static libraries (linux)."
  echo "  LINUX_PLUGIN_MODES      : Use auto, static, or dynamic plugin loading (linux)."
  echo "  LINUX_TCLIBCS           : Set the C library for the target (linux)."
  echo "  LINUX_TOOLCHAIN         : Set the Yocto toolchain to use (linux)."
  echo "  LINUX_TOOLCHAIN_VERSION : Set the Yocto toolchain version to use (linux)."
  echo "  USE_DOCKER              : Use docker to build the project (linux, windows)."
  echo "  MACOS_ARCHS             : Set the machine architecture to build for (macos)."
  echo "  MACOS_BUILD_SHARED      : Build shared or static libraries (macos)."
  echo "  MACOS_PLUGIN_MODES      : Use auto, static, or dynamic plugin loading (macos)."
  echo "  WINDOWS_ARCHS           : Set the machine architecture to build for (windows)."
  echo "  WINDOWS_BUILD_SHARED    : Build shared or static libraries (windows)."
  echo "  WINDOWS_PLUGIN_MODES    : Use auto, static, or dynamic plugin loading (windows)."
  echo "  OSS                     : Set the operating systems to build for."
  echo "  USE_PATCHED_CONAN_DEPS  : Use patched Boost and Ncurses overrides (default: ${default_use_patched_conan_deps})."
  echo "  WARNINGS_AS_ERRORS      : Treat project warnings as errors (default: ${default_warnings_as_errors})."
  echo "  PATCHED_CONAN_CHANNEL   : Channel used for patched deps (default: ${default_patched_conan_channel})."
  echo "  PATCHED_BOOST_VERSION   : Override Boost version (default: ${default_patched_boost_version})."
  echo "  PATCHED_NCURSES_VERSION : Override Ncurses version (default: ${default_patched_ncurses_version})."
  echo ""
  echo "Example:"
  echo ""
  echo "Build and export 'rstream-utils' for deployment:"
  echo ""
  echo "EXPORT_PACKAGE_NAME=\"rstream-utils\" LINUX_TCLIBCS=\"musl\" LINUX_BUILD_SHARED=\"off\" MACOS_BUILD_SHARED=\"on\" WINDOWS_BUILD_SHARED=\"off\" $0"
  echo ""
  echo "Build and export 'rstream-webtty' for deployment:"
  echo ""
  echo "EXPORT_PACKAGE_NAME=\"rstream-webtty\" LINUX_TCLIBCS=\"musl\" LINUX_BUILD_SHARED=\"off\" MACOS_BUILD_SHARED=\"off\" WINDOWS_BUILD_SHARED=\"off\" $0"
  echo ""
  echo "Build and export 'rstream-utils' under docker using the Yocto ${default_linux_toolchain_version} toolchain for ARMv8 with glibc using shared libraries (linux):"
  echo ""
  echo "EXPORT_PACKAGE_NAME=\"rstream-utils\" OSS=\"linux\" USE_DOCKER=\"on\" LINUX_TOOLCHAIN=\"yocto-toolchain\" LINUX_TOOLCHAIN_VERSION=\"${default_linux_toolchain_version}\" LINUX_ARCHS=\"arm64\" LINUX_TCLIBCS=\"glibc\" LINUX_BUILD_SHARED=\"on\" $0"
  echo ""
  echo "Build and export 'rstream-utils' for Apple M1 using shared libraries (macos):"
  echo ""
  echo "EXPORT_PACKAGE_NAME=\"rstream-utils\" OSS=\"macos\" MACOS_ARCHS=\"apple-m1\" MACOS_BUILD_SHARED=\"on\" $0"
  echo ""
  echo "Build and export 'rstream-utils' for arm64 using static libraries (windows):"
  echo ""
  echo "EXPORT_PACKAGE_NAME=\"rstream-utils\" OSS=\"windows\" WINDOWS_ARCHS=\"arm64\" WINDOWS_BUILD_SHARED=\"off\" $0"
  echo ""
}

case "$1" in
build | export | upload)
  CMD="$1" run
  ;;
"")
  CMD="build" run && CMD="export" run
  ;;
help | -h | --help)
  show_help
  ;;
*)
  echo "Invalid option: '$1'"
  echo ""
  show_help
  exit 1
  ;;
esac
