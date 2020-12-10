#!/bin/sh
#
# SPDX-License-Identifier: AGPL-3.0+
#
# Copyright (C) 2018 Ultimaker B.V.
# Copyright (C) 2018 Olliver Schinagl <oliver@schinagl.nl>
#

ARCH="${ARCH:-arm64}"
LOCAL_REGISTRY_IMAGE="ulticontroller_interface_tester"
PKG_CONFIG_PATH="${PKG_CONFIG_PATH:-/usr/lib/arm-linux-gnueabihf/pkgconfig/}"
WORK_DIR="${WORK_DIR:-/workdir}"
RELEASE_VERSION="${RELEASE_VERSION:-}"


set -eu


update_docker_image()
{
    echo "Building local Docker build environment."
    docker build . -t "${LOCAL_REGISTRY_IMAGE}"
}

run_in_docker()
{
    docker run \
        --privileged \
        --rm \
        -it \
        -u "$(id -u)" \
        -e "ARCH=${ARCH}" \
        -e "PKG_CONFIG_PATH=${PKG_CONFIG_PATH}" \
        -e "RELEASE_VERSION=${RELEASE_VERSION}" \
        -v "$(pwd):${WORK_DIR}" \
        -w "${WORK_DIR}" \
        "${LOCAL_REGISTRY_IMAGE}" \
        "${@}"
}

build()
{
    echo "Starting build"
    run_in_docker "./build.sh" "${@}"
    echo "Build complete"
}

if ! command -V docker; then
    echo "Docker not found, docker-less builds are not supported."
    exit 1
fi

update_docker_image
build "${@}"

exit 0
