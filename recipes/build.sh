#!/usr/bin/env bash
# Copyright The Phorepoc Authors
# SPDX-License-Identifier: GPL-2.0-only
#
# build.sh -- compiles the vendored Linux drivers (linux/, unmodified) against the shim (shim/include) and the shim
# runtime + glue, for ARM, with the toolchain image's arm-none-eabi-gcc. Objects go to $OUT (default build/arm).
# Then it links them into one relocatable object (camera-a64.o) and lists the symbols still undefined
# (camera-a64.undef): the runtime the Symbian kernel side has to provide.
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
CFLAGS=(-march=armv7-a -marm -mfloat-abi=soft -mno-unaligned-access -ffreestanding -fno-builtin -fno-common
  -fno-strict-aliasing -fno-delete-null-pointer-checks -fwrapv -O2 -std=gnu11 -Wall -Wno-unused-function
  -Wno-unused-variable -Wno-unused-but-set-variable -Wno-address-of-packed-member
  -include "$REPO/shim/include/phe_config.h" -I"$REPO/linux/include" -I"$REPO/linux/include/uapi" -I"$REPO/shim/include")

SRCS=(
  linux/drivers/i2c/algos/i2c-algo-bit.c
  linux/drivers/i2c/busses/i2c-gpio.c
  linux/drivers/media/i2c/ov5640.c
  linux/drivers/media/i2c/gc2145.c
  linux/drivers/media/platform/sunxi/sun6i-csi/sun6i_csi.c
  linux/drivers/media/platform/sunxi/sun6i-csi/sun6i_csi_bridge.c
  linux/drivers/media/platform/sunxi/sun6i-csi/sun6i_csi_capture.c
)
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

"$LD" -r -o "$OUT/camera-a64.o" "${objs[@]}"
"$NM" -u "$OUT/camera-a64.o" | awk '{print $2}' | sort -u > "$OUT/camera-a64.undef"
echo "linked $OUT/camera-a64.o; $(wc -l < "$OUT/camera-a64.undef") undefined symbols ($OUT/camera-a64.undef)"
