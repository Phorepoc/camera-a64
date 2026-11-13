#!/usr/bin/env bash
# Copyright The Phorepoc Authors
# SPDX-License-Identifier: GPL-2.0-only
#
# sis.sh -- the pdk's package.yml runs it after the components: $PHOREPOC_BUILD/sis/camera-a64-arm.sis with the camera
# server (phecamserver.exe), the ECam plug-in (phecama64.dll + its ECom registration) and the licences, NOTICE and
# written source offer (to \resource\camera-a64\licences\).
# Capabilities: the server needs CommDD (the CamHw.A64 channel), the plug-in ALL -TCB (it loads into any camera client).
# Neither is user-grantable, so the package installs where the installer trusts it with them (Phorepoc's developer
# mode, the default; or a trusted signature).
# The OV5640 autofocus firmware is a separate package (ov5640-firmware); without it the rear camera has fixed focus.
set -eu
[ "${PHOREPOC_ARCH:-arm}" = arm ] || { echo "sis.sh: camera-a64 is arm only"; exit 1; }
# Link stubs: a name no provider exports is linked to a stub that panics the caller (EUSRNIMP); none is allowed here
for img in phecamserver.exe phecama64.dll; do
  s=$PHOREPOC_BUILD/arm/img/$img.linkstubs
  [ -f "$s" ] || { echo "sis.sh: no $s (the emitter's link-stub record)"; exit 1; }
  if grep -q . "$s"; then echo "sis.sh: $img has link stubs:"; cat "$s"; exit 1; fi
done
REPO=$(cd "$(dirname "$0")/.." && pwd)
LIC=
for f in "$REPO"/LICENSES/* "$REPO/LICENSE" "$REPO/NOTICE" "$REPO/SOURCE-OFFER.txt"; do
  LIC="$LIC --file $f=!:/resource/camera-a64/licences/$(basename "$f")"
done
python3 "$PHOREPOC_PDK/tools/make_sis.py" phecamserver_arm phecama64_arm --arm --unsigned --uid 0xE7015C65 \
  --version "${PHOREPOC_PACKAGE_VERSION:-1.0.0}" --name "PinePhone cameras" --vendor Phorepoc $LIC \
  --out "$PHOREPOC_BUILD/sis/camera-a64-arm.sis"
