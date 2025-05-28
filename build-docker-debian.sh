#!/bin/bash

docker buildx build . -f docker/Dockerfile.debian -t registry.rstream.io/rstream-cpp-debian:$(cat version.txt)-snapshot-$(date +%F) -t registry.rstream.io/rstream-cpp-debian:latest --load $@
