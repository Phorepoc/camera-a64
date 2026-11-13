/* Copyright The Phorepoc Authors
 * SPDX-License-Identifier: GPL-2.0-only */

/* phe_config.h -- pre-included into every file: the kernel configuration the drivers see (Kconfig equivalents).
 * Debug-only options (CONFIG_VIDEO_ADV_DEBUG, the i2c-gpio fault injector) and the A31/V3s ISP link are off. */
#ifndef PHE_CONFIG_H
#define PHE_CONFIG_H
#define __KERNEL__ 1
#define __LITTLE_ENDIAN 1234
#define __LITTLE_ENDIAN_BITFIELD 1
#define KBUILD_MODNAME "camera_a64"
#define KBUILD_BASENAME "camera_a64"
#define CONFIG_MEDIA_CONTROLLER 1
#define CONFIG_VIDEO_V4L2_SUBDEV_API 1
#define CONFIG_VIDEO_V4L2_I2C 1
#define CONFIG_MMU 1
#define CONFIG_OF 1
#define CONFIG_PM 1
#endif
