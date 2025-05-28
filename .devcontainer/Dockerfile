# See here for image contents: https://github.com/microsoft/vscode-dev-containers/tree/v0.195.0/containers/cpp/.devcontainer/base.Dockerfile
# [Choice] Debian / Ubuntu version (use Debian 11/9, Ubuntu 18.04/21.04 on local arm64/Apple Silicon): debian-11, debian-10, debian-9, ubuntu-21.04, ubuntu-20.04, ubuntu-18.04

ARG VARIANT=ubuntu-22.04

FROM --platform=linux/amd64 mcr.microsoft.com/vscode/devcontainers/cpp:0-${VARIANT}

ENV DEBIAN_FRONTEND noninteractive

RUN apt-get update && apt-get -y install --no-install-recommends build-essential curl file git ruby-full locales sudo

RUN localedef -i en_US -f UTF-8 en_US.UTF-8

RUN useradd -m -s /bin/bash linuxbrew

RUN echo 'linuxbrew ALL=(ALL) NOPASSWD:ALL' >> /etc/sudoers

RUN usermod -a -G linuxbrew vscode

USER vscode

RUN /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/master/install.sh)"

ENV PATH="/home/linuxbrew/.linuxbrew/bin:/home/linuxbrew/.linuxbrew/sbin:${PATH}"

ENV LD_LIBRARY_PATH="/home/linuxbrew/.linuxbrew/lib/:${PATH}"

ENV HOMEBREW_NO_ANALYTICS=1

ENV HOMEBREW_NO_AUTO_UPDATE=1
