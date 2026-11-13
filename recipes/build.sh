#!/usr/bin/env bash
# Copyright The Phorepoc Authors
# SPDX-License-Identifier: GPL-2.0-only
#
# build.sh -- compiles the vendored Linux drivers (linux/, unmodified) against the shim (shim/include) and the shim
# runtime + glue (glue/*.c), for ARM, with the toolchain image's arm-none-eabi-gcc. Objects go to $OUT (default
# build/arm). Out: lib/libphecamlinux.a (the camera server links it: registry/components_camera.py), and
# camera-a64.o + camera-a64.undef (one relocatable object and the symbols it still needs: the server's Symbian side,
# os/src/phecamserver/phe_user.cpp, and libgcc).
#   usage: recipes/build.sh            (in ghcr.io/phorepoc/base, or any shell with arm-none-eabi-gcc on the PATH)
#          KEEP_GOING=1 recipes/build.sh   (compile every file, report all failures)
set -u
REPO=$(cd "$(dirname "$0")/.." && pwd)
OUT=${OUT:-$REPO/build/arm}
CC=${CC:-arm-none-eabi-gcc}
LD=${LD:-arm-none-eabi-ld}
NM=${NM:-arm-none-eabi-nm}
mkdir -p "$OUT"

# The Symbian ARM kernel's code-generation flags (armv7-a, ARM state, no unaligned access), freestanding C.
CFLAGS=(-march=armv7-a -marm -mfloat-abi=soft -mno-unaligned-access -fshort-wchar -ffreestanding -fno-builtin -fno-common -fno-short-enums
  -fno-strict-aliasing -fno-delete-null-pointer-checks -fwrapv -O2 -std=gnu11 -Wall -Wno-unused-function
  -Wno-unused-variable -Wno-unused-but-set-variable -Wno-address-of-packed-member
  -include "$REPO/shim/include/phe_config.h" -I"$REPO/linux/include" -I"$REPO/linux/include/uapi" -I"$REPO/shim/include")

# every vendored .c (the drivers and the V4L2 / videobuf2 / media-controller core), then ours
SRCS=($(cd "$REPO" && find linux/drivers -name '*.c' | LC_ALL=C sort))
for f in "$REPO"/shim/rt/*.c "$REPO"/glue/*.c; do [ -f "$f" ] && SRCS+=("${f#$REPO/}"); done

fail=0; objs=()
for s in "${SRCS[@]}"; do
  o="$OUT/$(basename "${s%.c}").o"
  if "$CC" "${CFLAGS[@]}" -c "$REPO/$s" -o "$o" 2> "$o.log"; then
    objs+=("$o"); echo "ok    $s"
  else
    fail=$((fail + 1)); echo "FAIL  $s ($(grep -c 'error:' "$o.log") errors, $o.log)"
    [ "${KEEP_GOING:-0}" = 1 ] || { head -40 "$o.log"; exit 1; }
  fi
done

[ "$fail" = 0 ] || { echo "$fail file(s) failed"; exit 1; }

AR=${AR:-arm-none-eabi-ar}
mkdir -p "$OUT/lib"
rm -f "$OUT/lib/libphecamlinux.a"
"$LD" -r -o "$OUT/camera-a64.o" "${objs[@]}"
# one member: the drivers register through the phe_drivers / phe_initcalls sections, which nothing references by
# name, so per-file members would be left out by the link; the server's call to phe_camera_init pulls in the lot
"$AR" rcs "$OUT/lib/libphecamlinux.a" "$OUT/camera-a64.o"
"$NM" -u "$OUT/camera-a64.o" | awk '{print $2}' | sort -u > "$OUT/camera-a64.undef"
echo "linked $OUT/camera-a64.o; $(wc -l < "$OUT/camera-a64.undef") undefined symbols ($OUT/camera-a64.undef)"
