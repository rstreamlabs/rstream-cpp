#!/bin/bash

set -e

script_dir=$(cd $(dirname "${BASH_SOURCE[0]}") && pwd)
docker_compose_file="${DOCKER_COMPOSE_FILE-${script_dir}/compose.yaml}"

docker compose -f ${docker_compose_file} run --rm -v ${script_dir}/config:/conan-config --entrypoint conan conan2-builder config install /conan-config
