FROM alpine:latest AS alpine

RUN apk update && apk add libstdc++

FROM ubuntu:24.04

ENV DEBIAN_FRONTEND noninteractive

# Install build tools
RUN apt-get update && apt-get -y install --no-install-recommends \
    build-essential \
    ca-certificates \
    ccache \
    cmake \
    curl \
    file \
    git \
    gzip \
    jq \
    ninja-build \
    pkg-config \
    python3-dev \
    python3-pip \
    tar \
    xz-utils

RUN apt-get update && apt-get -y install --no-install-recommends nasm

ARG LLVM_MINGW_VERSION=20251118

RUN set -eux; \
    host_arch="$(dpkg --print-architecture)"; \
    case "${host_arch}" in \
      amd64) llvm_mingw_host_arch="x86_64" ;; \
      arm64) llvm_mingw_host_arch="aarch64" ;; \
      *) echo "unsupported host architecture for llvm-mingw: ${host_arch}" >&2; exit 1 ;; \
    esac; \
    llvm_mingw_dir="/opt/llvm-mingw-${LLVM_MINGW_VERSION}-ucrt-ubuntu-22.04-${llvm_mingw_host_arch}"; \
    curl -fsSL "https://github.com/mstorsjo/llvm-mingw/releases/download/${LLVM_MINGW_VERSION}/llvm-mingw-${LLVM_MINGW_VERSION}-ucrt-ubuntu-22.04-${llvm_mingw_host_arch}.tar.xz" | tar -xJf - -C /opt; \
    ln -s "${llvm_mingw_dir}" /opt/llvm-mingw

ENV PATH="/opt/llvm-mingw/bin:${PATH}"

# Run binaries linked with musl
RUN apt-get update && apt-get -y install --no-install-recommends musl-dev

COPY --from=alpine /usr/lib/libstdc++.so.6 /usr/lib/*-linux-musl/libstdc++.so.6

COPY --from=alpine /usr/lib/libgcc_s.so.1 /usr/lib/*-linux-musl/libgcc_s.so.1

# Install conan
ARG CONAN_VERSION=2.26.2

RUN PIP_BREAK_SYSTEM_PACKAGES=1 python3 -m pip install --upgrade "conan==${CONAN_VERSION}" PyYAML

RUN conan profile detect

COPY config /tmp/conan-config

RUN conan config install /tmp/conan-config

RUN rm -rf /tmp/conan-config

ARG CONAN_REMOTE_NAME=rstream

ARG CONAN_REMOTE_URL

ARG CONAN_REMOTE_USERNAME

RUN if [ -n "${CONAN_REMOTE_URL}" ]; then \
      conan remote list | grep -q "^${CONAN_REMOTE_NAME}:" && conan remote update "${CONAN_REMOTE_NAME}" --url "${CONAN_REMOTE_URL}" || conan remote add "${CONAN_REMOTE_NAME}" "${CONAN_REMOTE_URL}"; \
    fi

RUN --mount=type=secret,id=password if [ -n "${CONAN_REMOTE_URL}" ] && [ -n "${CONAN_REMOTE_USERNAME}" ]; then \
      conan remote login "${CONAN_REMOTE_NAME}" "${CONAN_REMOTE_USERNAME}" -p "$(cat /run/secrets/password)"; \
    fi

RUN git config --global --add safe.directory /source
