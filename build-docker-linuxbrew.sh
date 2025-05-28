#!/bin/bash

docker buildx build . -f docker/Dockerfile.linuxbrew -t registry.rstream.io/rstream-cpp-linuxbrew:$(cat version.txt)-snapshot-$(date +%F) -t registry.rstream.io/rstream-cpp-linuxbrew:latest --load $@
