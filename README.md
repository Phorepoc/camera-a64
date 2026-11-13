# camera-a64

The PinePhone (Allwinner A64) camera drivers for Phorepoc, ported one-to-one from Linux and delivered as a Catalogue
package.

| Path | What | Licence |
|---|---|---|
| `linux/` | The Linux files, **unmodified**, from megi's PinePhone kernel tree (`codeberg.org/megi/linux`, branch `orange-pi-7.3`, commit `88af81e226a88ae422a9c301246e7ae64cda0b75`): `sun6i-csi` (CSI controller), `ov5640` (rear), `gc2145` (front), `i2c-gpio` + `i2c-algo-bit` (the camera I2C bus), the media/V4L2 headers they include, and the device-tree sources they are configured from. `linux/SHA256SUMS` pins every file; `recipes/import_linux.sh` reproduces the import. | GPL-2.0 (per-file SPDX lines), uapi headers GPL-2.0 WITH Linux-syscall-note |
| `shim/` | Ours: the freestanding Linux-kernel API the drivers compile against (`shim/include`: `phe_core.h`, `phe_dev.h`, `phe_misc.h`, `phe_kern.h`, plus one-line `linux/*.h` redirects) and its portable runtime (`shim/rt`: device model, platform and I2C buses, fwnode, clk/reset/gpio/regulator/irq/DMA on `phe_hw.h`, printf). `shim/UNDEFINED.txt` is what the linked Linux side needs from the server. | GPL-2.0-only |
| `glue/` | Ours: the PinePhone's device tree as a static node table (`pinephone_dt.c`) and what a V4L2 application would do, in C (`phe_camera.c`: media links, pad formats, buffers, stream). | GPL-2.0-only |
| `os/src/phecamserver/` | Ours: the camera server, a user-side process that runs the Linux stack (`phecamserver.cpp`) and implements the shim's runtime and hardware calls on Symbian (`phe_user.cpp`) over the image's thin `CamHw.A64` channel (adaptation-a64): the CSI registers and DMA memory as shared chunks, clocks, reset, pins, GPIOs, PMIC rails, the CSI interrupt. `phecam_api.h` is its client API (Apache-2.0). | GPL-2.0-only |
| `os/src/phecama64/` | The ECam plug-in: the 2010 TestCamera plug-in with Phorepoc's edits (as the image's `phecam`), its pixel source the camera server (`phecamsource.cpp`). Registered as version 5, above the image's plug-in; core's `ecampluginsupport` accepts installed camera plug-ins. | Nokia-derived files EPL-2.0 OR GPL-2.0-or-later; ours Apache-2.0 |
| `registry/`, `recipes/` | The components (`components_camera.py`), `prebuild.sh` (the Linux stack as `libphecamlinux.a`), `build.sh`, `sis.sh`, `import_linux.sh`. | GPL-2.0-only |

No Linux code runs in the kernel: the GPL drivers run in the camera server process, and the kernel side is the image's
generic hardware channel.

## Build

In the toolchain image (`ghcr.io/phorepoc/base`), from the repository root:

    recipes/build.sh            # -> build/arm/lib/libphecamlinux.a, camera-a64.o and camera-a64.undef

The SIS (`camera-a64-arm.sis`: server, plug-in, licences) comes from the pdk's package.yml (release.yml):
`recipes/prebuild.sh`, the components of `package.conf`, then `recipes/sis.sh`. The package needs a core with
installed camera plug-ins (the listing's `min_core`) and an adaptation-a64 image with the `CamHw.A64` channel. The
server's CommDD and the plug-in's ALL -TCB are not user-grantable: the package installs where the installer trusts
it (Phorepoc's developer mode, the default).

## Rules

- Files under `linux/` are never edited. A fix goes to the shim or the glue; if a driver change is unavoidable it goes
  in `patches/` as a separate, reviewed patch applied at build time.
- Firmware: the OV5640 autofocus microcode (`ov5640_af.bin`, OmniVision's, published with no licence) is not part of
  this package. Without it the driver runs with fixed focus (`ov5640_af_init` only warns), and the port keeps that
  fallback. The GC2145 driver's optional `gc2145-init.bin` override does not exist as a published file; the driver's
  built-in register tables are used.
