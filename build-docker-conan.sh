#!/bin/bash

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

script_dir=$(cd $(dirname "${BASH_SOURCE[0]}") && pwd)

declare -a toolchains=("glibc" "musl")

declare -a targets=("linux/amd64" "linux/arm64/v8" "linux/arm/v7" "linux/arm/v6")

if [ ! -z $1 ]; then toolchains=(${1}); fi
if [ ! -z $2 ]; then targets=(${2}); fi
if [ ! -z $3 ]; then type=(${3}); fi

function package_name {
  echo "$(conan inspect ${script_dir} --format=json | jq -r .name)"
}

function package_version {
  echo "$(conan inspect ${script_dir} --format=json | jq -r .version)"
}

snapshot="snapshot-$(date +%F)"

function output {
  if [[ $type == *"local"* ]]; then
    echo "type=${type},dest=out/release/$1"
  else
    echo "type=${type}"
  fi
}

for toolchain in ${toolchains[@]}; do
  docker buildx build ${script_dir} --progress plain --secret id=password,src=$HOME/.credentials/conan -o $(output $(package_version)) --platform "$(
    IFS=,
    echo "${targets[*]}"
  )" -f ${script_dir}/docker/Dockerfile.conan.${toolchain} -t registry.rstream.io/rstream-cpp-conan:$(package_version)-${toolchain}-${snapshot} -t registry.rstream.io/rstream-cpp-conan:$(package_version)-${toolchain}-latest
done
