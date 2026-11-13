# symbian2010-camerasc

From the 2010 Symbian release, `oss.FCL.sf.os.kernelhwsrv` commit 511b58062131f5d609886f62215969d0dffcfb44
(2010-09-01, "Revert incorrect RCL_3 drop"), byte-for-byte:

- `include/d32camerasc.h`, `include/d32camerasc.inl` (from `kernel/eka/include/`): `RDevCameraSc`.
- `include/drivers/camerasc.h`, `include/drivers/camerasc.inl` (from `kernel/eka/include/drivers/`): `DCameraScPdd`.
- `template_pdd/camerasc.cpp`, `template_pdd/camerasc_plat.h` (from `bsptemplate/asspandvariant/template_variant/camerasc/`).

Licence, as the file headers say: "Copyright (c) 2004-2009 Nokia Corporation and/or its subsidiary(-ies). ... made
available under the terms of the License "Eclipse Public License v1.0"". EPL-1.0 only: there is no GPL Secondary
License (under EPL-2.0 only the Initial Contributor can designate one), and EPL-1.0 and GPL-2.0 are incompatible.
`template_pdd/` is therefore NOT compiled into the GPL camera object (recipes/build.sh); see README "Licensing status".
