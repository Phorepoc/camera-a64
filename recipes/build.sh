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
CFLAGS=(-march=armv7-a -marm -mfloat-abi=soft -mno-unaligned-access -ffreestanding -fno-builtin -fno-common -fno-short-enums
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

# the Symbian kernel side (C++): our glue + the 2010 template camera PDD (unmodified), with the flags adaptation-a64's
# link_ekern.sh uses for its kernel objects. EPOC32 = an epoc32 tree with include/ (the core-arm image's /build/epoc32).
EPOC32=${EPOC32:-/build/epoc32}
CAMSC="$REPO/third_party/symbian2010-camerasc"
CXX=${CXX:-arm-none-eabi-g++}
TC=${TC:-/build/toolchain}		# symbian-2010/toolchain: symbian_gcc_prefix.h, overlay/, kinc/
EI="$EPOC32/include"
KFLAGS=(-march=armv7-a -marm -mfloat-abi=soft -mno-unaligned-access -fshort-wchar -fno-short-enums -fno-exceptions
  -fno-rtti -fpermissive -fno-lifetime-dse -fno-strict-aliasing -fno-delete-null-pointer-checks -fno-builtin -O2
  -include "$TC/symbian_gcc_prefix.h"
  -D__SYMBIAN32__ -D__GCCE__ -D__EABI__ -D__MARM__ -D__EPOC32__ -D_UNICODE -D__KERNEL_MODE__ -D__IN_KERNEL__
  -D__MARM_ARMV7__ -D__DEMAND_PAGING__ -D__MEMMODEL_FLEXIBLE__ -D__A64__
  -D__SMP__ -D__CPU_HAS_VFP -D__VFP_V3 -D__CPU_CORTEX_A9__ -D__CPU_ARM_HAS_GIC -D__CPU_ARM_HAS_GENERIC_TIMER
  -I"$TC/overlay" -I"$EI" -I"$EI/kernel" -I"$EI/memmodel" -I"$EI/nkernsmp" -I"$EI/nkernsmp/arm" -I"$EI/kernel/arm"
  -I"$EI/memmodel/epoc" -I"$TC/kinc" -I"$CAMSC/include" -I"$CAMSC/template_pdd" -I"$REPO/glue" -Wno-attributes)
for s in "$REPO"/glue/*.cpp; do	# not template_pdd/camerasc.cpp: EPL-1.0, cannot be linked with GPL code
  [ -f "$s" ] || continue
  o="$OUT/$(basename "${s%.cpp}").o"
  if "$CXX" "${KFLAGS[@]}" -c "$s" -o "$o" 2> "$o.log"; then
    objs+=("$o"); echo "ok    ${s#$REPO/}"
  else
    fail=$((fail + 1)); echo "FAIL  ${s#$REPO/} ($(grep -c 'error:' "$o.log") errors, $o.log)"
    [ "${KEEP_GOING:-0}" = 1 ] || { head -40 "$o.log"; exit 1; }
  fi
done
[ "$fail" = 0 ] || { echo "$fail file(s) failed"; exit 1; }

"$LD" -r -o "$OUT/camera-a64.o" "${objs[@]}"
"$NM" -u "$OUT/camera-a64.o" | awk '{print $2}' | sort -u > "$OUT/camera-a64.undef"
echo "linked $OUT/camera-a64.o; $(wc -l < "$OUT/camera-a64.undef") undefined symbols ($OUT/camera-a64.undef)"
