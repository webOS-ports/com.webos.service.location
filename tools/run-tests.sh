#!/bin/bash
# Full local verification for com.webos.service.location:
#   1. -Werror cross-compile for every supported architecture
#   2. unit tests, run for the x86-64 sysroot through its dynamic loader
#   3. the same unit tests under valgrind
#
# Usage: tools/run-tests.sh
set -u

HERE=$(cd "$(dirname "$0")" && pwd)
SRC=$(dirname "$HERE")
WORKBASE=${WORKBASE:-/media/herrie/LuneOS/wrynose/webos-ports/tmp/work}
PV=${PV:-1.0.0-108}
FAILED=0

ARCHES="aarch64-halium-webos-linux aarch64-webos-linux \
cortexa8t2hf-neon-halium-webos-linux-gnueabi corei7-64-webos-linux"

for a in $ARCHES; do
    echo "=== build $a (WERROR) ==="
    if ! WERROR=1 "$HERE/build-arch.sh" "$a"; then
        FAILED=1
    fi
done

SYSROOT=$WORKBASE/corei7-64-webos-linux/com.webos.service.location/$PV/recipe-sysroot
LOADER=$SYSROOT/usr/lib/ld-linux-x86-64.so.2
BUILD=$SRC/build-corei7-64-webos-linux
export TMPDIR=${TMPDIR:-/tmp}

run_test() {
    echo "=== run $1 ==="
    if ! "$LOADER" --library-path "$SYSROOT/usr/lib:$SYSROOT/lib" "$BUILD/$1"; then
        FAILED=1
    fi
}

for t in test_db_util test_gps_cfg test_ntp_packet; do
    run_test $t
done

if command -v valgrind >/dev/null; then
    echo "=== valgrind test_db_util ==="
    if ! valgrind --error-exitcode=42 --leak-check=full \
            --errors-for-leak-kinds=definite --quiet \
            "$LOADER" --library-path "$SYSROOT/usr/lib:$SYSROOT/lib" \
            "$BUILD/test_db_util" > /dev/null; then
        FAILED=1
        echo "valgrind reported errors"
    else
        echo "valgrind clean"
    fi
fi

[ "$FAILED" -eq 0 ] && echo "=== ALL PASS ===" || echo "=== FAILURES ==="
exit $FAILED
