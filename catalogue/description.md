The PinePhone's camera drivers, taken one-to-one from the Linux kernel the PinePhone distributions use: the A64 camera
interface (sun6i-csi), the OV5640 rear camera, the GC2145 front camera and the bit-banged camera I2C bus. They run in a
camera server process on top of a small compatibility layer, the system's camera hardware channel gives them the
camera block, and the system camera framework (ECam) reaches them through a camera plug-in.

Status: in development. The drivers build; they have not been run on a phone yet.

The rear camera works with fixed focus. Autofocus needs the OV5640 autofocus firmware (ov5640_af.bin), which is not part
of this package.

Licence: GPL-2.0 (the Linux drivers' licence). The source is at https://github.com/Phorepoc/camera-a64.
