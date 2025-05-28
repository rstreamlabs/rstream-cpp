#!/bin/bash

set -e

script_dir=$(cd $(dirname "${BASH_SOURCE[0]}") && pwd)

# Default values
default_build_type="Release"
default_compose_file="${script_dir}/conan/compose.yaml"
default_export_package_name="rstream-utils"
default_linux_archs=("x86_i686" "x86_core2" "x86_64" "x86_64_v2" "x86_64_v3" "x86_64_v4" "armv6" "armv6hf" "armv7" "armv7hf" "arm64" "mips" "mipsle" "mips64" "mips64le" "riscv64")
default_linux_build_shared=("on" "off")
default_linux_tclibcs=("glibc" "musl")
default_linux_toolchain_version="5.0.2"
default_linux_toolchain="yocto-toolchain"
default_macos_archs=("x86_64" "x86_64_v2" "x86_64_v3" "x86_64_v4" "arm64" "apple-m1" "apple-m2")
default_macos_build_shared=("on" "off")
default_oss=("macos" "linux" "windows")
default_use_docker="on"
default_windows_archs=("x86_64" "x86_64_v2" "x86_64_v3" "x86_64_v4")
default_windows_build_shared=("on" "off")

# Allow overrides from environment variables
build_type="${BUILD_TYPE:-${default_build_type}}"
docker_compose_file="${DOCKER_COMPOSE_FILE-${default_compose_file}}"
export_package_name="${EXPORT_PACKAGE_NAME:-${default_export_package_name}}"
linux_archs=("${LINUX_ARCHS[@]:-${default_linux_archs[@]}}")
linux_build_shared=("${LINUX_BUILD_SHARED[@]:-${default_linux_build_shared[@]}}")
linux_tclibcs=("${LINUX_TCLIBCS[@]:-${default_linux_tclibcs[@]}}")
linux_toolchain_version="${LINUX_TOOLCHAIN_VERSION:-${default_linux_toolchain_version}}"
linux_toolchain="${LINUX_TOOLCHAIN:-${default_linux_toolchain}}"
macos_archs=("${MACOS_ARCHS[@]:-${default_macos_archs[@]}}")
macos_build_shared=("${MACOS_BUILD_SHARED[@]:-${default_macos_build_shared[@]}}")
oss=("${OSS[@]:-${default_oss[@]}}")
use_docker="${USE_DOCKER:-${default_use_docker}}"
windows_archs=("${WINDOWS_ARCHS[@]:-${default_windows_archs[@]}}")
windows_build_shared=("${WINDOWS_BUILD_SHARED[@]:-${default_windows_build_shared[@]}}")

blacklist=()

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

declare -A extra_conan_options
extra_conan_options["linux-ppc64-glibc"]="--options boost/*:without_charconv=True"
extra_conan_options["linux-ppc64-musl"]="--options boost/*:without_charconv=True"
extra_conan_options["linux-ppc64le-glibc"]="--options boost/*:without_charconv=True"
extra_conan_options["linux-ppc64le-musl"]="--options boost/*:without_charconv=True"
extra_conan_options["linux-riscv64-glibc"]="--options openssl/*:no_asm=True"
extra_conan_options["linux-riscv64-musl"]="--options openssl/*:no_asm=True"

function shared {
  [ "${BUILD_SHARED}" = "on" ] && echo "True" || echo "False"
}

function package_name {
  echo "\$(conan inspect ${SRC_PATH} --format=json | jq -r .name)"
}

function package_version {
  echo "\$(conan inspect ${SRC_PATH} --format=json | jq -r .version)"
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
  [ "${LIBC}" = "musl" ] && echo $([ "${BUILD_SHARED}" = "on" ] && echo "False" || echo "True") || echo "False"
}

function windows_static_libstdcxx {
  [ "${BUILD_SHARED}" = "on" ] && echo "False" || echo "True"
}

function package_options {
  echo "--options $(package_name)/*:shared=$(shared)"
}

function linux_package_options {
  echo "$(package_options) --options $(package_name)/*:static_libstdcxx=$(${OS}_static_libstdcxx)"
}

function macos_package_options {
  echo "$(package_options)"
}

function windows_package_options {
  echo "$(package_options) --options $(package_name)/*:static_libstdcxx=$(${OS}_static_libstdcxx)"
}

function linux_conan_extra_options {
  echo "${extra_conan_options["${OS}-${ARCH}-${LIBC}"]} --options boost/*:without_stacktrace=True"
}

function macos_conan_extra_options {
  echo "${extra_conan_options["${OS}-${ARCH}"]}"
}

function windows_conan_extra_options {
  echo "${extra_conan_options["${OS}-${ARCH}-${LIBC}"]} --options boost/*:without_stacktrace=True"
}

function conan_options {
  echo "$(${OS}_conan_extra_options) $(${OS}_package_options) --profile:build=default --settings:host build_type=${build_type}"
}

function linux_conan_options {
  echo "$(conan_options) --profile:host=${default_linux_toolchain} --settings:host arch=$(conan_arch) --settings:host os.sdk=${default_linux_toolchain}-${default_linux_toolchain_version}-${ARCH}-${LIBC} --options:build ${default_linux_toolchain}/${default_linux_toolchain_version}:arch=${ARCH} --options:build ${default_linux_toolchain}/${default_linux_toolchain_version}:libc=${LIBC}"
}

function macos_conan_options {
  echo "$(conan_options) --profile:host=${OS}-${ARCH}"
}

function windows_conan_options {
  echo "$(conan_options) --profile:host=${OS}-${ARCH}"
}

function linux_package_outdir {
  echo "out/release/$(package_version)/${OS}/${ARCH}/${LIBC}/$([ "${BUILD_SHARED}" == "on" ] && echo "shared" || echo "static")"
}

function macos_package_outdir {
  echo "out/release/$(package_version)/${OS}/${ARCH}/$([ "${BUILD_SHARED}" == "on" ] && echo "shared" || echo "static")"
}

function windows_package_outdir {
  echo "out/release/$(package_version)/${OS}/${ARCH}/$([ "${BUILD_SHARED}" == "on" ] && echo "shared" || echo "static")"
}

function cmd_build {
  echo "conan create $(${OS}_conan_options) -u --build=$(package_name) --build=missing ${SRC_PATH}"
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
  echo "EXPORT_PACKAGE_NAME=${export_package_name} conan install $(${OS}_conan_options) --requires $(package_name)/$(package_version) --deployer ${SRC_PATH}/deploy.py -of ${SRC_PATH}/$(${OS}_package_outdir)"
}

function linux_cmd_export {
  echo "${extra_env_vars["${OS}-${ARCH}-${LIBC}"]} $(cmd_export)"
}

function macos_cmd_export {
  echo "${extra_env_vars["${OS}-${ARCH}"]} $(cmd_export)"
}

function windows_cmd_export {
  echo "${extra_env_vars["${OS}-${ARCH}"]} $(cmd_export)"
}

function linux_run_docker {
  docker compose -f ${docker_compose_file} run --rm --entrypoint "bash" -v "${script_dir}:/source:rw" -e CHANNEL -e VERSION -e OS -e ARCH -e LIBC -e BUILD_SHARED -e SRC_PATH "${@:2}" conan2-builder -c "$1"
}

function windows_run_docker {
  docker compose -f ${docker_compose_file} run --rm --entrypoint "bash" -v "${script_dir}:/source:rw" -e CHANNEL -e VERSION -e OS -e ARCH -e BUILD_SHARED -e SRC_PATH "${@:2}" conan2-builder -c "$1"
}

function linux_run_build {
  if [ "${use_docker}" = "on" ]; then
    export SRC_PATH=/source
    ${OS}_run_docker "$(${OS}_cmd_build)"
  else
    export SRC_PATH=${script_dir}
    bash -c "$(${OS}_cmd_build)"
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
  bash -c "$(${OS}_cmd_build)"
}

function windows_run_build {
  if [ "${use_docker}" = "on" ]; then
    export SRC_PATH=/source
    ${OS}_run_docker "$(${OS}_cmd_build)"
  else
    export SRC_PATH=${script_dir}
    bash -c "$(${OS}_cmd_build)"
  fi
}

function linux_run_export {
  if [ "${use_docker}" = "on" ]; then
    export SRC_PATH=/source
    ${OS}_run_docker "$(${OS}_cmd_export) && chown -R $(id -u):$(id -g) /source/out"
  else
    export SRC_PATH=${script_dir}
    bash -c "$(${OS}_cmd_export)"
  fi
}

function macos_run_export {
  export SRC_PATH=${script_dir}
  bash -c "$(${OS}_cmd_export)"
}

function windows_run_export {
  if [ "${use_docker}" = "on" ]; then
    export SRC_PATH=/source
    ${OS}_run_docker "$(${OS}_cmd_export) && chown -R $(id -u):$(id -g) /source/out"
  else
    export SRC_PATH=${script_dir}
    bash -c "$(${OS}_cmd_export)"
  fi
}

function linux_get_outdir {
  if [ "${use_docker}" = "on" ]; then
    export SRC_PATH=/source
    ${OS}_run_docker "echo $(${OS}_package_outdir)"
  else
    export SRC_PATH=${script_dir}
    bash -c "echo $(${OS}_package_outdir)"
  fi
}

function macos_get_outdir {
  export SRC_PATH=${script_dir}
  bash -c "echo $(${OS}_package_outdir)"
}

function windows_get_outdir {
  if [ "${use_docker}" = "on" ]; then
    export SRC_PATH=/source
    ${OS}_run_docker "echo $(${OS}_package_outdir)"
  else
    export SRC_PATH=${script_dir}
    bash -c "echo $(${OS}_package_outdir)"
  fi
}

function linux_get_version {
  if [ "${use_docker}" = "on" ]; then
    export SRC_PATH=/source
    ${OS}_run_docker "echo $(package_version)"
  else
    export SRC_PATH=${script_dir}
    bash -c "echo $(package_version)"
  fi
}

function macos_get_version {
  export SRC_PATH=${script_dir}
  bash -c "echo $(package_version)"
}

function windows_get_version {
  if [ "${use_docker}" = "on" ]; then
    export SRC_PATH=/source
    ${OS}_run_docker "echo $(package_version)"
  else
    export SRC_PATH=${script_dir}
    bash -c "echo $(package_version)"
  fi
}

function run_upload {
  export SRC_PATH=${script_dir}
  if [ -f .env.local ]; then
    source .env.local
  fi
  outdir=$(${OS}_get_outdir)
  extension=$([ "${OS}" = "windows" ] && echo ".zip" || echo ".tar.gz")
  archive="${outdir}/packages/${export_package_name}${extension}"
  name="${export_package_name}"
  version=$(${OS}_get_version)
  channel=$([ "${RSTREAM_URL}" = "https://rstream.io" ] && echo "stable" || echo "dev")
  shared=$([ "${BUILD_SHARED}" = "on" ] && echo "true" || echo "false")
  if [ "${OS}" = "linux" ]; then
    filename="${export_package_name}-${version}-${OS}-${ARCH}-${LIBC}-$([ "${BUILD_SHARED}" == "on" ] && echo "shared" || echo "static")${extension}"
  else
    filename="${export_package_name}-${version}-${OS}-${ARCH}-$([ "${BUILD_SHARED}" == "on" ] && echo "shared" || echo "static")${extension}"
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
  curl --progress-bar --upload-file "$(pwd)/${archive}" -fail -H "Content-Type: application/octet-stream" -X PUT "${signed_url}" | cat
  printf "name:${name}\nid:${package_id}\nversion:${version}\nchannel:${channel}\nos:${OS}\narch:${ARCH}\nshared:${shared}\nfilename:${filename}\nchecksum:${checksum}\n" >$(pwd)/${archive}.info
  if [ "${OS}" = "linux" ]; then
    printf "libc:${LIBC}\n" >>$(pwd)/${archive}.info
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
  if [ "${use_docker}" != "on" ] && [ "$(uname -o)" != "GNU/Linux" ]; then
    echo "Linux build must be run on Linux or with docker"
    exit 1
  fi
  for arch in "${linux_archs[@]}"; do
    for libc in "${linux_tclibcs[@]}"; do
      config="${OS}-${arch}-${libc}"
      if [[ " ${blacklist[@]} " =~ " ${config} " ]]; then
        echo "Skipping blacklisted configuration: ${config}"
        continue
      fi
      for build_shared in ${linux_build_shared[@]}; do
        ARCH=${arch} LIBC=${libc} BUILD_SHARED=${build_shared} ${OS}_run_${CMD}
        if [ "$?" -ne 0 ]; then
          echo "Failed to ${CMD} for ${config} with shared libraries ${build_shared}"
          exit 1
        fi
      done
    done
  done
}

function macos_run {
  if [ "$(uname -o)" != "Darwin" ]; then
    echo "macOS build must be run on macOS"
    exit 1
  fi
  for arch in "${macos_archs[@]}"; do
    config="${OS}-${arch}"
    if [[ " ${blacklist[@]} " =~ " ${config} " ]]; then
      echo "Skipping blacklisted configuration: ${config}"
      continue
    fi
    for build_shared in ${macos_build_shared[@]}; do
      ARCH=${arch} BUILD_SHARED=${build_shared} ${OS}_run_${CMD}
      if [ "$?" -ne 0 ]; then
        echo "Failed to ${CMD} for ${config} with shared libraries ${build_shared}"
        exit 1
      fi
    done
  done
}

function windows_run {
  if [ "${use_docker}" != "on" ] && [ "$(uname -o)" != "GNU/Linux" ]; then
    echo "Windows build must be run on Linux or with docker"
    exit 1
  fi
  for arch in "${windows_archs[@]}"; do
    config="${OS}-${arch}"
    if [[ " ${blacklist[@]} " =~ " ${config} " ]]; then
      echo "Skipping blacklisted configuration: ${config}"
      continue
    fi
    for build_shared in ${windows_build_shared[@]}; do
      ARCH=${arch} BUILD_SHARED=${build_shared} ${OS}_run_${CMD}
      if [ "$?" -ne 0 ]; then
        echo "Failed to ${CMD} for ${config} with shared libraries ${build_shared}"
        exit 1
      fi
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
  for os in "${oss[@]}"; do
    OS=${os} ${os}_run
  done
}

function show_help {
  echo "Conan Builder - Automated builder for conan"
  echo ""
  echo "Usage: $0 [build|export|upload|-h]"
  echo ""
  echo "  build             : Build the project using the Yocto SDK."
  echo "  export            : Export the build output to the out directory."
  echo "  upload            : Upload the exported build output."
  echo "  -h                : Show this help message."
  echo ""
  echo "Environmesnt Variables:"
  echo ""
  echo "  DOCKER_COMPOSE_FILE     : Set the docker-compose file to use (linux)."
  echo "  EXPORT_PACKAGE_NAME     : Set the package name to export."
  echo "  LINUX_ARCHS             : Set the machine architecture to build for (linux)."
  echo "  LINUX_BUILD_SHARED      : Build shared or static libraries (linux)."
  echo "  LINUX_TCLIBCS           : Set the C library for the target (linux)."
  echo "  LINUX_TOOLCHAIN         : Set the Yocto toolchain to use (linux)."
  echo "  LINUX_TOOLCHAIN_VERSION : Set the Yocto toolchain version to use (linux)."
  echo "  USE_DOCKER              : Use docker to build the project (linux, windows)."
  echo "  MACOS_ARCHS             : Set the machine architecture to build for (macos)."
  echo "  MACOS_BUILD_SHARED      : Build shared or static libraries (macos)."
  echo "  WINDOWS_ARCHS           : Set the machine architecture to build for (windows)."
  echo "  WINDOWS_BUILD_SHARED    : Build shared or static libraries (windows)."
  echo "  OSS                     : Set the operating systems to build for."
  echo ""
  echo "Example:"
  echo ""
  echo "Build and export 'rstream-utils' for deployment:"
  echo ""
  echo "EXPORT_PACKAGE_NAME=\"rstream-utils\" LINUX_TCLIBCS=\"musl\" LINUX_BUILD_SHARED=\"off\" MACOS_BUILD_SHARED=\"on\" WINDOWS_BUILD_SHARED=\"off\" $0"
  echo ""
  echo "Build and export 'rstream-rtty' for deployment:"
  echo ""
  echo "EXPORT_PACKAGE_NAME=\"rstream-rtty\" LINUX_TCLIBCS=\"musl\" LINUX_BUILD_SHARED=\"off\" MACOS_BUILD_SHARED=\"off\" WINDOWS_BUILD_SHARED=\"off\" $0"
  echo ""
  echo "Build and export 'rstream-utils' under docker using the Yocto ${default_linux_toolchain_version} toolchain for ARMv8 with glibc using shared libraries (linux):"
  echo ""
  echo "EXPORT_PACKAGE_NAME=\"rstream-utils\" OSS=\"linux\" USE_DOCKER=\"on\" LINUX_TOOLCHAIN=\"yocto-toolchain\" LINUX_TOOLCHAIN_VERSION=\"${default_linux_toolchain_version}\" LINUX_ARCHS=\"arm64\" LINUX_TCLIBCS=\"glibc\" LINUX_BUILD_SHARED=\"on\" $0"
  echo ""
  echo "Build and export 'rstream-utils' for Apple M1 using shared libraries (macos):"
  echo ""
  echo "EXPORT_PACKAGE_NAME=\"rstream-utils\" OSS=\"macos\" MACOS_ARCHS=\"apple-m1\" MACOS_BUILD_SHARED=\"on\" $0"
  echo ""
  echo "Build and export 'rstream-utils' for x86_64 using static libraries (windows):"
  echo ""
  echo "EXPORT_PACKAGE_NAME=\"rstream-utils\" OSS=\"windows\" WINDOWS_ARCHS=\"x86_64\" WINDOWS_BUILD_SHARED=\"off\" $0"
  echo ""
}

case "$1" in
build | export | upload)
  CMD="$1" run
  ;;
"")
  CMD="build" run && CMD="export" run
  ;;
-h)
  show_help
  ;;
*)
  echo "Invalid option: '$1'"
  echo ""
  show_help
  exit 1
  ;;
esac
