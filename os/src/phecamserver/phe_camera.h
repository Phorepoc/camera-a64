/* Copyright The Phorepoc Authors
 * SPDX-License-Identifier: GPL-2.0-only */

/* phe_camera.h -- the C interface the camera server (phecamserver.cpp) uses to drive the Linux camera stack
 * (phe_camera.c). Units: 0 = rear OV5640 (CSI endpoint 0), 1 = front GC2145 (endpoint 1). Codes: V4L2 fourccs and
 * media-bus codes. Negative returns are Linux errnos. Calls are made from the server's capture thread. */
#ifndef PHE_CAMERA_H
#define PHE_CAMERA_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
int phe_camera_init(void);
int phe_camera_count(void);
int phe_camera_configure(int unit, unsigned int width, unsigned int height, uint32_t pixelformat, uint32_t mbus_code,
	unsigned int *bytesperline, unsigned int *sizeimage);
int phe_camera_start(unsigned int nbufs);
int phe_camera_next_frame(void *dst, unsigned int len);
int phe_camera_stop(void);
int phe_camera_ctrl(int unit, uint32_t id, int32_t value);
#ifdef __cplusplus
}
#endif
#endif
