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
struct phe_camera_ctrl_info {		/* struct v4l2_queryctrl, the fields a client needs */
	uint32_t type, flags, menu_skip_mask;
	int32_t minimum, maximum, step, def;
};
int phe_camera_init(void);
int phe_camera_count(void);
int phe_camera_configure(int unit, unsigned int width, unsigned int height, uint32_t pixelformat, uint32_t mbus_code,
	unsigned int fps,	/* 0 = leave the frame rate */
	unsigned int *bytesperline, unsigned int *sizeimage);
int phe_camera_start(unsigned int nbufs);
int phe_camera_rotation(int unit);	/* V4L2_CID_CAMERA_SENSOR_ROTATION, degrees CCW; 0 if the sensor has none */
int phe_camera_next_frame(void *dst, unsigned int len);
int phe_camera_stop(int keep_power);	/* 1 = keep the sensor powered until the next start */
int phe_camera_ctrl(int unit, uint32_t id, int32_t value);
int phe_camera_get_ctrl(int unit, uint32_t id, int32_t *value);
int phe_camera_query_ctrl(int unit, uint32_t id, struct phe_camera_ctrl_info *info);
int phe_camera_enum_size(int unit, unsigned int index, uint32_t mbus_code, unsigned int *width, unsigned int *height);
int phe_camera_menu_value(int unit, uint32_t id, unsigned int index, int64_t *value);
#ifdef __cplusplus
}
#endif
#endif
