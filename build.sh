#!/bin/sh
#
# Copyright (C) 2018 Ultimaker B.V.
# Copyright (C) 2018 Olliver Schinagl <oliver@schinagl.nl>
#

set -eu

ARCH="${ARCH:-arm64}"
CWD="$(pwd)"

BUILD_DIR="${CWD}/.build_${ARCH}"

cleanup=true

cleanup()
{
    if ! ${cleanup}; then
        return
    fi

    echo "Cleaning up '${BUILD_DIR}'."
    if [ -d "${BUILD_DIR}" ] && [ -z "${BUILD_DIR##*/.build*}" ]; then
        rm -rf "${BUILD_DIR}"
    fi

}

build_prepare()
{
    echo "Preparing build in '${BUILD_DIR}'."
    if [ ! -e "${BUILD_DIR}" ]; then
        mkdir -p "${BUILD_DIR}"
    fi
}

build()
{
    echo "Building software in '${BUILD_DIR}'."
    cmake \
        -DCPACK_PACKAGE_VERSION="${RELEASE_VERSION:-}" \
        -DCMAKE_C_COMPILER="aarch64-linux-gnu-gcc" \
        -DARCH="${ARCH}" \
        -H"." \
        -B"${BUILD_DIR}"
    cmake --build "${BUILD_DIR}"
}

build_artifact()
{
    echo "Building artifact."
    if [ -e $.deb ]; then
        rm ./*.deb
    fi

    make -C "${BUILD_DIR}" package

    echo "Package '$(basename "$(ls "$BUILD_DIR/"*.deb)")' created."
    cp "${BUILD_DIR}/"*.deb .
}

usage()
{
    echo "Usage: ${0} [OPTIONS]"
    echo "    -c    Skip cleanup (incremental builds, failure checking)"
    echo "    -h    Print this help and exit"
}

while getopts ":hc" options; do
    case "${options}" in
    c)
        cleanup=false
        ;;
    h)
        usage
        exit 0
        ;;
    :)
        echo "Option -${OPTARG} requires an argument."
        exit 1
        ;;
    ?)
        echo "Invalid option: -${OPTARG}"
        exit 1
        ;;
    esac
done
shift "$((OPTIND - 1))"

trap cleanup EXIT

cleanup
build_prepare
build
build_artifact

exit 0
