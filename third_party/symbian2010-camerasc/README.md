# symbian2010-camerasc

From the 2010 Symbian release (kernelhwsrv, via adaptation-a64's `third_party/symbian2010-camerasc`), unmodified:

- `include/d32camerasc.h`, `include/d32camerasc.inl`: the shared-chunk camera channel (`RDevCameraSc`) interface.
- `include/drivers/camerasc.h`, `include/drivers/camerasc.inl`: the LDD/PDD contract (`DCameraScPdd`).
- `template_pdd/camerasc.cpp`, `template_pdd/camerasc_plat.h`: the template camera PDD (factory and PDD classes).
  Its sensor class `DTemplateSensorIf` is implemented by `glue/phe_sensor.cpp` over the Linux drivers.

Licence: Eclipse Public License 2.0, with GPL-2.0-or-later as a Secondary License (see each file's header), which is
what lets these files be combined with the GPL-2.0 Linux code in this package.
