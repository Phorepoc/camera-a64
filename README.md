# camera-a64

The PinePhone (Allwinner A64) camera drivers for Phorepoc, ported one-to-one from Linux and delivered as a Catalogue
package.

| Path | What | Licence |
|---|---|---|
| `linux/` | The Linux files, **unmodified**, from megi's PinePhone kernel tree (`codeberg.org/megi/linux`, branch `orange-pi-7.3`, commit `88af81e226a88ae422a9c301246e7ae64cda0b75`): `sun6i-csi` (CSI controller), `ov5640` (rear), `gc2145` (front), `i2c-gpio` + `i2c-algo-bit` (the camera I2C bus), the media/V4L2 headers they include, and the device-tree sources they are configured from. `linux/SHA256SUMS` pins every file; `recipes/import_linux.sh` reproduces the import. | GPL-2.0 (per-file SPDX lines), uapi headers GPL-2.0 WITH Linux-syscall-note |
| `shim/` | Ours: the freestanding Linux-kernel API the drivers compile against (`shim/include`: `phe_core.h`, `phe_dev.h`, `phe_misc.h`, plus one-line `linux/*.h` redirects). `shim/UNDEFINED.txt` is what the linked drivers still need from outside. | GPL-2.0-only |
| `recipes/` | `build.sh` (compile + link + undefined-symbol list), `import_linux.sh` (the vendoring). | GPL-2.0-only |

Planned (not here yet): `shim/rt` (the runtime on the Symbian kernel), the vendored V4L2/videobuf2/media-controller core
`.c` files, `glue/` (a `DCameraScPdd` over the V4L2 subdevices and the vb2 queue, the board's device-tree data as a
static node table) and an ECam plugin.

## Build

In the toolchain image (`ghcr.io/phorepoc/base`), from the repository root:

    recipes/build.sh            # -> build/arm/camera-a64.o and build/arm/camera-a64.undef

## Rules

- Files under `linux/` are never edited. A fix goes to the shim or the glue; if a driver change is unavoidable it goes
  in `patches/` as a separate, reviewed patch applied at build time.
- The OV5640 autofocus firmware and the GC2145 init file are not redistributable and are never committed: the user
  supplies them.
