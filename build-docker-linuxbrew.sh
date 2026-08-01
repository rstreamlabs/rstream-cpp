#!/usr/bin/env bash
set -euo pipefail

docker buildx build . \
  -f docker/Dockerfile.linuxbrew \
  -t "registry.rstream.io/rstream-cpp-linuxbrew:$(cat version.txt)-snapshot-$(date +%F)" \
  -t registry.rstream.io/rstream-cpp-linuxbrew:latest \
  --load \
  "$@"
