#!/bin/bash

set -e

script_dir="$(cd $(dirname "${BASH_SOURCE[0]}") && pwd)"
build_name="${build_name:-$(uname -m)-$(uname -s)$(uname -r)}"
build_type="${build_type:-Debug}"
workdir="${workdir:-${script_dir}/build-${build_name}}"
release_dir="${release_dir:-${workdir}/release}"
build_dir="${build_dir:-${workdir}/build}"
cmake_generator="${cmake_generator:-Ninja}"
source_dir="${source_dir:-${script_dir}}"

cmake -G ${cmake_generator} -DCMAKE_INSTALL_PREFIX=${release_dir} -DCMAKE_BUILD_TYPE=${build_type} -DBUILD_SHARED_LIBS=TRUE -S ${source_dir} -B ${build_dir} $@
cmake --build ${build_dir}
cmake --build ${build_dir} --target check
cmake --build ${build_dir} --target install
