FROM registry.hub.docker.com/library/debian:stable-slim

LABEL Maintainer="software-embedded-platform@ultimaker.com" \
      Comment="Ultimaker kernel build environment"

RUN dpkg --add-architecture armhf && \
    apt-get update && \
    apt-get install -q -y --no-install-recommends \
        build-essential \
        cmake \
        crossbuild-essential-armhf \
        git \
        libevdev-dev:armhf \
        pkg-config \
    && \
    apt-get clean && \
    rm -rf /var/lib/apt/lists/

ENV PKG_CONFIG_PATH="${PKG_CONFIG_PATH:-/usr/lib/arm-linux-gnueabihf/pkgconfig/}"
ENV CROSS_COMPILE="${CROSS_COMPILE:-arm-linux-gnueabihf-}"
COPY test/buildenv.sh /test/buildenv.sh
