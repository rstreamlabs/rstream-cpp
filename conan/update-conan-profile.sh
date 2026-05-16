#!/usr/bin/env bash

set -euo pipefail

script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
docker_compose_file="${DOCKER_COMPOSE_FILE-${script_dir}/compose.yaml}"

docker compose -f "${docker_compose_file}" run --rm \
  -v "${script_dir}/config:/conan-config:ro" \
  -e CONAN_REMOTE_NAME \
  -e CONAN_REMOTE_URL \
  -e CONAN_REMOTE_USERNAME \
  --entrypoint bash \
  conan2-builder \
  -c 'set -euo pipefail
      conan profile detect -f
      conan config install /conan-config
      if [ -n "${CONAN_REMOTE_URL:-}" ]; then
        remote_name="${CONAN_REMOTE_NAME:-rstream}"
        conan remote list | grep -q "^${remote_name}:" && conan remote update "${remote_name}" --url "${CONAN_REMOTE_URL}" || conan remote add "${remote_name}" "${CONAN_REMOTE_URL}"
        if [ -n "${CONAN_REMOTE_USERNAME:-}" ] && [ -s /run/secrets/password ]; then
          conan remote login "${remote_name}" "${CONAN_REMOTE_USERNAME}" -p "$(cat /run/secrets/password)"
        fi
      fi'
