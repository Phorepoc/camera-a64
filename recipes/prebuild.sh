#!/usr/bin/env bash
# Copyright The Phorepoc Authors
# SPDX-License-Identifier: GPL-2.0-only
#
# prebuild.sh -- the pdk's package.yml runs it in the toolchain container before the components: the Linux camera
# stack (recipes/build.sh) as $PHOREPOC_BUILD/thirdparty/arm/camera-a64/lib/libphecamlinux.a for the camera server.
set -eu
HERE=$(cd "$(dirname "$0")/.." && pwd)
[ "${PHOREPOC_ARCH:-arm}" = arm ] || { echo "camera-a64: arm only"; exit 1; }
export OUT="${PHOREPOC_BUILD:-/build}/thirdparty/arm/camera-a64"
command -v arm-none-eabi-gcc >/dev/null || export PATH=/opt/arm-gnu/bin:$PATH
exec bash "$HERE/recipes/build.sh"
