#!/bin/bash

set -e

script_dir=$(cd $(dirname "${BASH_SOURCE[0]}") && pwd)

docker compose -f ${script_dir}/compose.yaml run --rm -v ${script_dir}/config:/conan-config --entrypoint conan conan2-builder config install /conan-config
