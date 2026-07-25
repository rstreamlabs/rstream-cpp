#!/usr/bin/env bash
set -euo pipefail

# usage :
#
# ./build-docker-conan.sh glibc linux/amd64 docker
# ./build-docker-conan.sh glibc linux/arm64/v8 docker
# ./build-docker-conan.sh musl linux/amd64 local
# ./build-docker-conan.sh musl linux/arm64/v8 local
# ./build-docker-conan.sh emscripten linux/amd64 docker
# ./build-docker-conan.sh emscripten linux/arm64/v8 docker
# ./build-docker-conan.sh android linux/amd64 local
# ./build-docker-conan.sh android linux/arm64/v8 local
# ./build-docker-conan.sh mingw linux/amd64 local
# ./build-docker-conan.sh mingw linux/arm64/v8 local

type="local"
conan_password_file="${CONAN_PASSWORD_FILE:-$HOME/.credentials/conan}"
docker_build_args=()

script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)

declare -a toolchains=("glibc" "musl" "emscripten" "android" "mingw")

declare -a targets=("linux/amd64" "linux/arm64/v8")

if [ -n "${1:-}" ]; then read -r -a toolchains <<<"$1"; fi
if [ -n "${2:-}" ]; then read -r -a targets <<<"$2"; fi
if [ -n "${3:-}" ]; then type="$3"; fi

if [ -n "${CONAN_REMOTE_NAME:-}" ]; then docker_build_args+=(--build-arg "CONAN_REMOTE_NAME=${CONAN_REMOTE_NAME}"); fi
if [ -n "${CONAN_REMOTE_URL:-}" ]; then docker_build_args+=(--build-arg "CONAN_REMOTE_URL=${CONAN_REMOTE_URL}"); fi
if [ -n "${CONAN_REMOTE_USERNAME:-}" ]; then docker_build_args+=(--build-arg "CONAN_REMOTE_USERNAME=${CONAN_REMOTE_USERNAME}"); fi

function package_name {
  conan inspect "${script_dir}" --format=json | jq -r .name
}

function package_version {
  conan inspect "${script_dir}" --format=json | jq -r .version
}

snapshot="snapshot-$(date +%F)"

function output {
  if [[ $type == *"local"* ]]; then
    echo "type=${type},dest=out/release/$1"
  else
    echo "type=${type}"
  fi
}

# we require conan 2.0.0 or later
conan_version=$(conan --version | cut -d ' ' -f3)
if [ "$(printf '%s\n' "2.0.0" "${conan_version}" | sort -V | head -n1)" != "2.0.0" ]; then
  echo "Conan 2.0.0 or later is required"
  exit 1
fi

version=$(package_version)
platforms=$(IFS=,; echo "${targets[*]}")
for toolchain in "${toolchains[@]}"; do
  docker buildx build "${script_dir}" \
    --progress plain \
    --secret "id=password,src=${conan_password_file}" \
    "${docker_build_args[@]}" \
    -o "$(output "${version}")" \
    --platform "${platforms}" \
    -f "${script_dir}/docker/Dockerfile.conan.${toolchain}" \
    -t "registry.rstream.io/rstream-cpp-conan:${version}-${toolchain}-${snapshot}" \
    -t "registry.rstream.io/rstream-cpp-conan:${version}-${toolchain}-latest"
done
