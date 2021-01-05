FROM registry.hub.docker.com/library/debian:stable-slim

LABEL Maintainer="software-embedded@ultimaker.com" \
      Comment="Ultimaker kernel build environment"

RUN dpkg --add-architecture arm64 && \
    apt-get update && \
    apt-get install -q -y --no-install-recommends \
        build-essential \
        gcc-aarch64-linux-gnu \
        cmake \
        crossbuild-essential-arm64 \
        git \
        libevdev-dev:arm64 \
        make \
        libevdev-dev \
        pkg-config \
    && \
    apt-get clean && \
    rm -rf /var/lib/apt/lists/
