# Copyright The Phorepoc Authors
# SPDX-License-Identifier: GPL-2.0-only

r"""components_camera -- the camera-a64 layer's components (merged into core's registry when this repository is built:
PHOREPOC_ADAPTATIONS=<adaptation-a64>:<camera-a64>, see the pdk's tools/emitter/paths.py).

The Linux camera stack is not built by the registry: recipes/build.sh (run by recipes/prebuild.sh) compiles it into
$PHOREPOC_BUILD/thirdparty/arm/camera-a64/lib/libphecamlinux.a, which the server links through arm_archives.
Its hardware channel's header (a64camhw_api.h) comes from adaptation-a64 (src/phorepoc-arm/a64/camera), which the
mmp reaches in the merged OS tree.

Both components ship only in the SIS (recipes/sis.sh); neither is a ROM component."""
import os

from paths import OS_ROOT

COMPONENTS = {
    # the camera server: the Linux drivers (sun6i-csi, ov5640, gc2145, i2c-gpio) + V4L2 core in a user-side process
    "phecamserver_arm": dict(mmp=os.path.join(OS_ROOT, "src/phecamserver/phecamserver.mmp"), rom="phecamserver.exe",
                             device=True, allow_wsd=True,
                             arm_archives=["thirdparty/arm/camera-a64/lib/libphecamlinux.a"]),
    # the ECam plug-in over it (version 5: above the image's phecam plug-in)
    "phecama64_arm": dict(mmp=os.path.join(OS_ROOT, "src/phecama64/phecama64.mmp"), rom="phecama64.dll", device=True),
}

# loaded from RAM (installed on C:): `sis` lays the images out for the loader's RAM checks
for _k in COMPONENTS:
    COMPONENTS[_k].setdefault("sis", True)
