#!/bin/bash
# Cross-compile com.webos.service.location against the LuneOS/Yocto recipe
# sysroots without going through bitbake.  Gives a fast per-architecture
# compile check with a stricter warning set than the recipe uses.
#
# Usage: tools/build-arch.sh <arch-tuple>
#   aarch64-halium-webos-linux                      sargo / halium-arm64
#   aarch64-webos-linux                             generic arm64
#   cortexa8t2hf-neon-halium-webos-linux-gnueabi    armv7 (thumb2, neon)
#   corei7-64-webos-linux                           x86-64
#
# Env: WERROR=1 to fail on any warning, BUILD=<dir> to relocate the build tree.
set -u

WORKBASE=${WORKBASE:-/media/herrie/LuneOS/wrynose/webos-ports/tmp/work}
PV=${PV:-1.0.0-108}
PN=com.webos.service.location
ARCH=${1:?usage: build-arch.sh <arch-tuple>}

W=$WORKBASE/$ARCH/$PN/$PV
SYSROOT=$W/recipe-sysroot
NATIVE=$W/recipe-sysroot-native
[ -d "$SYSROOT" ] || { echo "no sysroot for $ARCH at $SYSROOT"; exit 2; }

TRIPLE=$(basename "$(find "$NATIVE/usr/bin" -maxdepth 1 -mindepth 1 -type d -name '*-webos-linux*' | head -1)")
CROSS=$NATIVE/usr/bin/$TRIPLE/$TRIPLE
export PATH=$NATIVE/usr/bin/$TRIPLE:$NATIVE/usr/bin:$NATIVE/usr/sbin:$NATIVE/bin:$NATIVE/sbin:$PATH

# Some recipe-sysroot-native trees carry only binutils; the compiler came from a
# shared sstate component.  Fall back to tmp/sysroots-components for it.
if [ ! -x "${CROSS}-gcc" ]; then
    COMP=$(dirname "$WORKBASE")/sysroots-components/x86_64/gcc-cross-${TRIPLE%%-*}/usr/bin/$TRIPLE
    if [ -x "$COMP/$TRIPLE-gcc" ]; then export PATH="$COMP:$PATH"; CROSS=$COMP/$TRIPLE; fi
fi
[ -x "${CROSS}-gcc" ] || { echo "no cross gcc: ${CROSS}-gcc"; exit 2; }

SRC=$(cd "$(dirname "$0")/.." && pwd)
BUILD=${BUILD:-$SRC/build-$ARCH}

# Warning set the tree is required to stay clean under.
WARN="-Wall -Wextra -Wformat=2 -Wformat-security -Wformat-overflow=2 \
-Wstringop-overflow=4 -Wstringop-truncation -Warray-bounds=2 -Wvla \
-Wuninitialized -Wmaybe-uninitialized -Wreturn-type -Wshadow=local \
-Wpointer-arith -Wno-unused-parameter -Wno-psabi -Wno-deprecated-declarations"
[ "${WERROR:-0}" = 1 ] && WARN="$WARN -Werror"

export PKG_CONFIG_PATH=$SYSROOT/usr/lib/pkgconfig:$SYSROOT/usr/share/pkgconfig
export PKG_CONFIG_SYSROOT_DIR=$SYSROOT
export PKG_CONFIG_LIBDIR=$PKG_CONFIG_PATH

rm -rf "$BUILD"; mkdir -p "$BUILD"

# gcc looks up its assembler/linker under the unprefixed names.  The Yocto
# sysroots only ship the triple-prefixed ones, so build a -B directory of
# unprefixed symlinks; otherwise gcc silently picks the host /usr/bin/as.
BINDIR=$BUILD/toolchain-bin; mkdir -p "$BINDIR"
for t in as ld ld.bfd ar nm objcopy objdump ranlib strip; do
    for d in "$(dirname "$CROSS")" "$NATIVE/usr/bin/$TRIPLE" \
             "$(dirname "$WORKBASE")/sysroots-components/x86_64/binutils-cross-${TRIPLE%%-*}/usr/bin/$TRIPLE"; do
        if [ -x "$d/$TRIPLE-$t" ]; then ln -sf "$d/$TRIPLE-$t" "$BINDIR/$t"; break; fi
    done
done
WARN="$WARN -B$BINDIR"


# The Yocto toolchain file pins bare compiler names (resolved against a PATH we
# do not inherit) and owns the -mcpu/-mfpu tuning.  Rewrite it with absolute
# compiler paths and our warning flags appended to, not replacing, the tuning.
sed -e "s|^set( CMAKE_C_COMPILER .*|set( CMAKE_C_COMPILER ${CROSS}-gcc )|" \
    -e "s|^set( CMAKE_CXX_COMPILER .*|set( CMAKE_CXX_COMPILER ${CROSS}-g++ )|" \
    -e "s|^set( CMAKE_ASM_COMPILER .*|set( CMAKE_ASM_COMPILER ${CROSS}-gcc )|" \
    -e "/CMAKE_C_COMPILER_LAUNCHER/d" -e "/CMAKE_CXX_COMPILER_LAUNCHER/d" \
    -e "s|\(^set( CMAKE_C_FLAGS \"[^\"]*\)\"|\1 $WARN\"|" \
    -e "s|\(^set( CMAKE_CXX_FLAGS \"[^\"]*\)\"|\1 $WARN\"|" \
    "$W/toolchain.cmake" > "$BUILD/toolchain-abs.cmake"

cmake -S "$SRC" -B "$BUILD" \
  -DCMAKE_TOOLCHAIN_FILE="$BUILD/toolchain-abs.cmake" \
  -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
  -DWEBOS_INSTALL_ROOT:PATH=/ \
  -DLOCATION_BUILD_TESTS=ON \
  > "$BUILD/cmake.log" 2>&1 || { echo "== cmake failed for $ARCH =="; tail -30 "$BUILD/cmake.log"; exit 1; }

make -C "$BUILD" -j"$(nproc)" > "$BUILD/compile.log" 2>&1
rc=$?
grep -E 'warning:|error:' "$BUILD/compile.log" | sed 's|'"$SRC"'/||'
echo "== $ARCH: warnings=$(grep -c 'warning:' "$BUILD/compile.log") errors=$(grep -c 'error:' "$BUILD/compile.log") rc=$rc =="
exit $rc
