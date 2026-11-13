#!/usr/bin/env bash
# Copyright The Phorepoc Authors
# SPDX-License-Identifier: GPL-2.0-only
#
# import_linux.sh -- how linux/ was produced: takes the driver files and the media/V4L2 headers they need from megi's
# kernel tree at one commit, unmodified, and writes linux/SHA256SUMS. Re-run it to move the pin (then rebuild and review
# the diff). Headers are followed recursively, but only inside media/ and the few linux/ headers that belong to the
# media and I2C bit-banging subsystems (with their uapi twins); every other <linux/...> include resolves to the shim
# (shim/include).
#   usage: recipes/import_linux.sh [commit]     (needs git; a blobless sparse clone, a few MB)
set -eu
COMMIT=${1:-88af81e226a88ae422a9c301246e7ae64cda0b75}   # megi/linux orange-pi-7.3, 2026-10-19
URL=https://codeberg.org/megi/linux.git
REPO=$(cd "$(dirname "$0")/.." && pwd)
DST=$REPO/linux
WORK=${WORK:-/tmp/camera-a64-import}
FILES="
drivers/i2c/algos/i2c-algo-bit.c
drivers/i2c/busses/i2c-gpio.c
drivers/media/i2c/ov5640.c
drivers/media/i2c/gc2145.c
drivers/media/platform/sunxi/sun6i-csi/Kconfig
drivers/media/platform/sunxi/sun6i-csi/Makefile
drivers/media/platform/sunxi/sun6i-csi/sun6i_csi.c
drivers/media/platform/sunxi/sun6i-csi/sun6i_csi.h
drivers/media/platform/sunxi/sun6i-csi/sun6i_csi_bridge.c
drivers/media/platform/sunxi/sun6i-csi/sun6i_csi_bridge.h
drivers/media/platform/sunxi/sun6i-csi/sun6i_csi_capture.c
drivers/media/platform/sunxi/sun6i-csi/sun6i_csi_capture.h
drivers/media/platform/sunxi/sun6i-csi/sun6i_csi_reg.h
arch/arm64/boot/dts/allwinner/sun50i-a64.dtsi
arch/arm64/boot/dts/allwinner/sun50i-a64-pinephone.dtsi
arch/arm64/boot/dts/allwinner/sun50i-a64-pinephone-1.2.dts
COPYING
LICENSES/preferred/GPL-2.0
LICENSES/exceptions/Linux-syscall-note
"
if [ ! -d "$WORK/.git" ]; then
  git init -q "$WORK"
  git -C "$WORK" remote add origin "$URL"
fi
git -C "$WORK" config core.sparseCheckout true
printf '%s\n' $FILES /include/media/ /include/linux/ /include/uapi/linux/ > "$WORK/.git/info/sparse-checkout"
git -C "$WORK" fetch -q --depth 1 --filter=blob:none origin "$COMMIT"
git -C "$WORK" checkout -q --force FETCH_HEAD

put() { mkdir -p "$DST/$(dirname "$1")"; cp "$WORK/$1" "$DST/$1"; }
for f in $FILES; do put "$f"; done

# Headers: the ones a vendored file includes that we vendor too (the rest is the shim's).
vendored() {
  case "$1" in
    media/*|linux/videodev2.h|linux/v4l2-*.h|linux/media.h|linux/media-bus-format.h|linux/i2c-algo-bit.h|linux/platform_data/i2c-gpio.h) return 0 ;;
  esac
  return 1
}
incs() { sed -n 's/^#include <\(.*\)>.*/\1/p' "$@"; }
seen=" "
queue=$(cd "$WORK" && incs $(echo "$FILES" | grep -E '\.[ch]$') | sort -u)
while [ -n "$queue" ]; do
  next=
  for h in $queue; do
    case "$seen" in *" $h "*) continue ;; esac
    seen="$seen$h "
    vendored "${h#uapi/}" || continue
    # <uapi/linux/x.h> = include/uapi/linux/x.h; <linux/x.h> = include/linux/x.h, plus its uapi twin when there is one
    # (some media headers are uapi-only)
    for p in include/$h include/uapi/${h#uapi/}; do
      case "$p" in include/uapi/uapi/*) continue ;; esac
      [ -f "$WORK/$p" ] || continue
      put "$p"
      next="$next $(incs "$WORK/$p")"
    done
  done
  queue=$(echo "$next" | tr ' ' '\n' | sort -u)
done
cd "$DST" && find . -type f ! -name SHA256SUMS ! -name README.md | sed 's#^\./##' | LC_ALL=C sort | xargs sha256sum > SHA256SUMS
echo "imported $(wc -l < SHA256SUMS) files at $COMMIT"
