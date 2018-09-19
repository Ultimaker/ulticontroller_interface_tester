#!/bin/sh
#
# SPDX-License-Identifier: AGPL-3.0+
#
# Copyright (C) 2018 Ultimaker B.V.
# Copyright (C) 2018 Olliver Schinagl <oliver@schinagl.nl>
#

ARCH="${ARCH:-armhf}"
CI_REGISTRY_IMAGE="${CI_REGISTRY_IMAGE:-registry.gitlab.com/ultimaker/embedded/platform/ulticontroller_interface_tester}"
CI_REGISTRY_IMAGE_TAG="${CI_REGISTRY_IMAGE_TAG:-latest}"
CROSS_COMPILE="${CROSS_COMPILE:-arm-linux-gnueabihf-}"
PKG_CONFIG_PATH="${PKG_CONFIG_PATH:-/usr/lib/arm-linux-gnueabihf/pkgconfig/}"
WORK_DIR="${WORK_DIR:-/workdir}"

set -eu

run_in_docker()
{
    docker run --rm -i -t -h "$(hostname)" -u "$(id -u):$(id -g)" \
        -e "ARCH=${ARCH}" \
        -e "CROSS_COMPILE=${CROSS_COMPILE}" \
        -e "PKG_CONFIG_PATH=${PKG_CONFIG_PATH}" \
        -e "RELEASE_VERSION=${RELEASE_VERSION:-}" \
        -e "MAKEFLAGS=-j$(($(getconf _NPROCESSORS_ONLN) - 1))" \
        -v "$(pwd):${WORK_DIR}" \
        -w "${WORK_DIR}" \
        "${CI_REGISTRY_IMAGE}:${CI_REGISTRY_IMAGE_TAG}" \
        "${@}"
}

run_in_shell()
{
    PKG_CONFIG_PATH="${PKG_CONFIG_PATH}" \
    CROSS_COMPILE="${CROSS_COMPILE}" \
        sh "${@}"
}

run_script()
{
    if ! command -V docker; then
        echo "Docker not found, attempting native run."

        run_in_shell "${@}"
    else
        run_in_docker "${@}"
    fi
}

build()
{
    echo "Starting build"
    run_script "./build.sh" "${@}"
    echo "Build complete"
}

env_check()
{
    echo "Checking environment support."
    run_script "./test/buildenv.sh"
}

env_check
build "${@}"

exit 0
