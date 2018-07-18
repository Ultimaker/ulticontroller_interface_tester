#!/bin/sh

set -eu

TEST_DIR=$(mktemp -d)

test_failed=false

cleanup()
{
    if [ "$(dirname "${TEST_DIR}")" != "/tmp" ]; then
        exit 1
    fi
    rm -rf "${TEST_DIR}"
}

trap cleanup EXIT

check_compiler()
{
    cat <<-EOT > "${TEST_DIR}/compiler_test.c"
	#include <libevdev/libevdev.h>

	int main(void)
	{
	    struct libevdev *dev = libevdev_new();

	    libevdev_free(dev);
	    return 0;
	}
EOT

    # shellcheck disable=SC2046
    # allow word splitting for pkg-config
    "${CROSS_COMPILE}gcc" \
        $(pkg-config --libs --cflags libevdev) \
        -o "${TEST_DIR}/compiler_test" \
        -c "${TEST_DIR}/compiler_test.c" \
        -Wall -Werror || test_failed=true

    "${CROSS_COMPILE}objdump" -dS "${TEST_DIR}/compiler_test" 1> /dev/null || test_failed=true

    printf "compiler and libevdev support: "
    if "${test_failed}"; then
        echo "error"
    else
      echo "ok"
    fi
}

check_compiler

if "${test_failed}"; then
   echo "ERROR: Missing preconditions, cannot continue."
   exit 1
fi

echo "All Ok"

exit 0
