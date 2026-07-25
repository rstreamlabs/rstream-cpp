#!/bin/bash

set -e

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
build_name="${build_name:-$(uname -m)-$(uname -s)$(uname -r)}"
build_type="${build_type:-Debug}"
workdir="${workdir:-${script_dir}/build-${build_name}}"
release_dir="${release_dir:-${workdir}/release}"
build_dir="${build_dir:-${workdir}/build}"
cmake_generator="${cmake_generator:-Ninja}"
source_dir="${source_dir:-${script_dir}}"
build_jobs="${build_jobs:-}"
clean_first="${clean_first:-0}"
ctest_parallel_level="${ctest_parallel_level:-}"
cmake_args=(
  -G "${cmake_generator}"
  -DCMAKE_INSTALL_PREFIX="${release_dir}"
  -DCMAKE_BUILD_TYPE="${build_type}"
  -DBUILD_SHARED_LIBS=TRUE
)
build_args=()
build_parallel_args=()

detect_build_jobs() {
  if command -v nproc >/dev/null 2>&1; then
    nproc
  elif [ "$(uname -s)" = "Darwin" ]; then
    sysctl -n hw.logicalcpu 2>/dev/null || sysctl -n hw.ncpu
  else
    getconf _NPROCESSORS_ONLN 2>/dev/null || echo 1
  fi
}

is_truthy() {
  case "$1" in
  1 | on | ON | true | TRUE | yes | YES) return 0 ;;
  *) return 1 ;;
  esac
}

sanitize_env_flags() {
  local name="$1"
  local value="${!name}"
  local keep=()
  local token
  for token in ${value}; do
    case "${token}" in
    -I/opt/homebrew/opt/openssl@1.1/include | -L/opt/homebrew/opt/openssl@1.1/lib | -I/usr/local/opt/openssl@1.1/include | -L/usr/local/opt/openssl@1.1/lib) ;;
    *) keep+=("${token}") ;;
    esac
  done
  printf -v "${name}" '%s' "${keep[*]}"
  export "${name?}"
}

if [ "$(uname -s)" = "Darwin" ]; then
  if [ ! -d /opt/homebrew/opt/openssl@1.1 ] && [ ! -d /usr/local/opt/openssl@1.1 ]; then
    sanitize_env_flags CFLAGS
    sanitize_env_flags CPPFLAGS
    sanitize_env_flags CXXFLAGS
    sanitize_env_flags LDFLAGS
  fi
  cmake_args+=(
    -DCMAKE_C_FLAGS="${CPPFLAGS} ${CFLAGS}"
    -DCMAKE_CXX_FLAGS="${CPPFLAGS} ${CXXFLAGS}"
    -DCMAKE_EXE_LINKER_FLAGS="${LDFLAGS}"
    -DCMAKE_SHARED_LINKER_FLAGS="${LDFLAGS}"
    -DCMAKE_MODULE_LINKER_FLAGS="${LDFLAGS}"
  )
fi
if [ -z "${build_jobs}" ]; then
  build_jobs="$(detect_build_jobs)"
fi
if [ -z "${ctest_parallel_level}" ]; then
  ctest_parallel_level="${build_jobs}"
fi
if is_truthy "${clean_first}"; then
  build_args+=(--clean-first)
fi
if [ -n "${build_jobs}" ]; then
  build_parallel_args=(--parallel "${build_jobs}")
fi

cmake "${cmake_args[@]}" -S "${source_dir}" -B "${build_dir}" "$@"
cmake --build "${build_dir}" "${build_args[@]}" "${build_parallel_args[@]}"
CTEST_PARALLEL_LEVEL="${ctest_parallel_level}" cmake --build "${build_dir}" --target check "${build_parallel_args[@]}"
cmake --build "${build_dir}" --target install "${build_parallel_args[@]}"
