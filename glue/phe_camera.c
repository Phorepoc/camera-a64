/* Copyright The Phorepoc Authors
 * SPDX-License-Identifier: GPL-2.0-only */

/* phe_camera.c -- what a Linux camera application does through V4L2 and the media controller (media-ctl + a capture
 * loop), done in the camera server (os/src/phecamserver): pick the sensor by enabling its link to the
 * CSI bridge, set the sensor and bridge pad formats, set the capture format, request and queue vb2 MMAP buffers,
 * stream on, dequeue filled frames and hand their bytes to the server, requeue. All calls go through the vendored
 * drivers' own V4L2 entry points (ioctl ops, subdev pad ops, vb2), so the Linux code runs as it does under Linux. */
#include "../shim/include/phe_linux.h"
#include <media/v4l2-device.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-subdev.h>
#include <media/videobuf2-v4l2.h>
#include "../linux/drivers/media/platform/sunxi/sun6i-csi/sun6i_csi.h"
#include "../linux/drivers/media/platform/sunxi/sun6i-csi/sun6i_csi_bridge.h"
#include "../linux/drivers/media/platform/sunxi/sun6i-csi/sun6i_csi_capture.h"
#include "../os/src/phecamserver/phe_camera.h"

int phe_camera_probe_all(void);
struct platform_device *phe_camera_csi_pdev(void);

static struct sun6i_csi_device *csi;
static struct inode cam_inode;
static struct file cam_file;
static struct v4l2_fh cam_fh;
static int streaming;
static struct v4l2_subdev *cur_sensor;	/* the sensor the last configure linked */
static struct device *held;		/* a runtime-PM reference kept across a stop (phe_camera_stop(1)) */
static void release_hold(void);

int phe_camera_init(void)
{
	int r = phe_camera_probe_all();
	if (r)
		return r;
	csi = platform_get_drvdata(phe_camera_csi_pdev());
	if (!csi)
		return -ENODEV;
	/* a file on the capture node, as open() would make: video_devdata() finds the device through the inode */
	cam_inode.i_rdev = MKDEV(VIDEO_MAJOR, csi->capture.video_dev.minor);
	cam_file.f_inode = &cam_inode;
	v4l2_fh_init(&cam_fh, &csi->capture.video_dev);
	cam_file.private_data = &cam_fh;
	return 0;
}

int phe_camera_count(void)
{
	int i, n = 0;
	for (i = 0; csi && i < SUN6I_CSI_SOURCE_PARALLEL_MAX; ++i)
		n += csi->bridge.source_parallel[i].subdev != NULL;
	return n;
}

/* the sensor of endpoint `unit` (0 = rear OV5640, 1 = front GC2145): its link on, the other off (shared bus) */
static struct v4l2_subdev *select_sensor(int unit)
{
	struct media_entity *bridge = &csi->bridge.subdev.entity;
	struct v4l2_subdev *chosen = NULL;
	struct media_link *link;
	list_for_each_entry(link, &bridge->links, list) {
		struct v4l2_subdev *sd;
		if (link->sink->entity != bridge)
			continue;
		sd = media_entity_to_v4l2_subdev(link->source->entity);
		media_entity_setup_link(link, sd == csi->bridge.source_parallel[unit].subdev ? MEDIA_LNK_FL_ENABLED : 0);
		if (sd == csi->bridge.source_parallel[unit].subdev)
			chosen = sd;
	}
	return chosen;
}

static int set_pad_format(struct v4l2_subdev *sd, unsigned int pad, struct v4l2_mbus_framefmt *mf)
{
	struct v4l2_subdev_state *state = v4l2_subdev_lock_and_get_active_state(sd);
	struct v4l2_subdev_format f = { .which = V4L2_SUBDEV_FORMAT_ACTIVE, .pad = pad, .format = *mf };
	int r = v4l2_subdev_call(sd, pad, set_fmt, state, &f);
	if (state)
		v4l2_subdev_unlock_state(state);
	if (!r)
		*mf = f.format;
	return r;
}

int phe_camera_configure(int unit, unsigned int width, unsigned int height, uint32_t pixelformat, uint32_t mbus_code,
	unsigned int fps, unsigned int *bytesperline, unsigned int *sizeimage)
{
	const struct v4l2_ioctl_ops *ops = csi->capture.video_dev.ioctl_ops;
	struct v4l2_mbus_framefmt mf = { .width = width, .height = height, .code = mbus_code, .field = V4L2_FIELD_NONE };
	struct v4l2_format fmt = { .type = V4L2_BUF_TYPE_VIDEO_CAPTURE };
	struct v4l2_subdev *sensor;
	int r;

	if (streaming)
		return -EBUSY;
	sensor = select_sensor(unit);
	cur_sensor = sensor;
	if (!sensor)
		return -ENODEV;
	if ((r = set_pad_format(sensor, 0, &mf)) != 0)
		return r;
	if (fps) {
		/* VIDIOC_SUBDEV_S_FRAME_INTERVAL, as Megapixels sets its capture-rate: OV5640 2592x1944 is 15 fps at most */
		struct v4l2_subdev_state *state = v4l2_subdev_lock_and_get_active_state(sensor);
		struct v4l2_subdev_frame_interval fi = { .pad = 0, .which = V4L2_SUBDEV_FORMAT_ACTIVE, .interval = { 1, fps } };
		int e = v4l2_subdev_call(sensor, pad, set_frame_interval, state, &fi);
		if (state)
			v4l2_subdev_unlock_state(state);
		shim_log("camera-a64: frame interval %u fps -> %d (now 1/%u)", fps, e, fi.interval.denominator);
	}
	if ((r = set_pad_format(&csi->bridge.subdev, SUN6I_CSI_BRIDGE_PAD_SINK, &mf)) != 0)
		return r;
	fmt.fmt.pix.width = mf.width;
	fmt.fmt.pix.height = mf.height;
	fmt.fmt.pix.pixelformat = pixelformat;
	fmt.fmt.pix.field = V4L2_FIELD_NONE;
	if ((r = ops->vidioc_s_fmt_vid_cap(&cam_file, &cam_fh, &fmt)) != 0)
		return r;
	*bytesperline = fmt.fmt.pix.bytesperline;
	*sizeimage = fmt.fmt.pix.sizeimage;
	return 0;
}

/* video_ioctl2() holds the video device's lock (= the vb2 queue's lock, &capture->lock) around every ioctl; vb2 relies on
 * it (wait_prepare/wait_finish drop and retake it while a DQBUF blocks), so our direct calls take it too */
static int locked_reqbufs(struct v4l2_requestbuffers *req)
{
	int r;
	mutex_lock(&csi->capture.lock);
	r = vb2_reqbufs(&csi->capture.queue, req);
	mutex_unlock(&csi->capture.lock);
	return r;
}
static int locked_qbuf(struct v4l2_buffer *b)
{
	int r;
	mutex_lock(&csi->capture.lock);
	r = vb2_qbuf(&csi->capture.queue, csi->media_dev, b);
	mutex_unlock(&csi->capture.lock);
	return r;
}

/* bring-up readback: the sensor registers that decide whether a DVP frame leaves the chip (OV5640 datasheet: 0x3008
 * system control, 0x300e MIPI/DVP select, 0x3017/0x3018 pad output enables, 0x4740 DVP polarity, 0x4300 format) */
static void __maybe_unused dump_sensor(unsigned int unit)
{
	/* + AEC/AGC (datasheet ch. 4.6): 0x3503 manual bits, 0x3a0f/10/1b/1e/11/1f stable/fast windows, 0x56a1 average
	 * luminance, 0x3500-02 exposure, 0x350a/b gain; 0x3406 AWB manual, 0x5001 ISP enables */
	static const u16 regs[] = { 0x300a, 0x3008, 0x300e, 0x3017, 0x3018, 0x4740, 0x4300, 0x501f, 0x3034, 0x3035, 0x3036, 0x3037, 0x3108,
		0x3503, 0x3a00, 0x3a0f, 0x3a10, 0x3a1b, 0x3a1e, 0x3a11, 0x3a1f, 0x56a1, 0x3500, 0x3501, 0x3502, 0x350a, 0x350b, 0x3406, 0x5001 };
	struct v4l2_subdev *sd = csi->bridge.source_parallel[unit].subdev;
	struct i2c_client *c = sd ? v4l2_get_subdevdata(sd) : NULL;
	unsigned int i;
	if (!c)
		return;
	for (i = 0; i < ARRAY_SIZE(regs); ++i) {
		u8 a[2] = { regs[i] >> 8, regs[i] & 0xff }, v = 0;
		struct i2c_msg m[2] = { { .addr = c->addr, .flags = 0, .len = 2, .buf = a }, { .addr = c->addr, .flags = I2C_M_RD, .len = 1, .buf = &v } };
		int r = i2c_transfer(c->adapter, m, 2);
		shim_log("camera-a64: sensor @%02x reg %04x = %02x (r %d)", c->addr, regs[i], v, r);
	}
}

int phe_camera_start(unsigned int nbufs)
{
	struct v4l2_requestbuffers req = { .count = nbufs, .type = V4L2_BUF_TYPE_VIDEO_CAPTURE, .memory = V4L2_MEMORY_MMAP };
	unsigned int i;
	int r;
	if ((r = locked_reqbufs(&req)) != 0) {
		shim_log("camera-a64: start: reqbufs %u -> %d", nbufs, r);
		return r;
	}
	for (i = 0; i < req.count; ++i) {
		struct v4l2_buffer b = { .index = i, .type = V4L2_BUF_TYPE_VIDEO_CAPTURE, .memory = V4L2_MEMORY_MMAP };
		if ((r = locked_qbuf(&b)) != 0) {
			shim_log("camera-a64: start: qbuf %u -> %d", i, r);
			return r;
		}
	}
	mutex_lock(&csi->capture.lock);
	r = vb2_streamon(&csi->capture.queue, V4L2_BUF_TYPE_VIDEO_CAPTURE);
	mutex_unlock(&csi->capture.lock);
	if (r != 0) {
		shim_log("camera-a64: start: streamon -> %d", r);
		return r;
	}
	shim_log("camera-a64: start: %u buffers, streaming", req.count);
	streaming = 1;
	release_hold();
	return 0;
}

/* one frame: dequeue (waits), copy into the server's buffer, requeue. Returns the bytes copied. */
int phe_camera_next_frame(void *dst, unsigned int len)
{
	struct v4l2_buffer b = { .type = V4L2_BUF_TYPE_VIDEO_CAPTURE, .memory = V4L2_MEMORY_MMAP };
	struct vb2_buffer *vb;
	unsigned int n;
	int r;
	if (!streaming)
		return -EINVAL;
	{
		static unsigned int calls;
		if (++calls <= 3)
			shim_log("camera-a64: next_frame #%u: dqbuf (streaming %d, done list %s, owned by driver %u, queued %u)", calls,
				csi->capture.state.streaming, list_empty(&csi->capture.queue.done_list) ? "empty" : "has buffers",
				atomic_read(&csi->capture.queue.owned_by_drv_count), csi->capture.queue.queued_count);
	}
	/* polled, not a blocking dqbuf: a mode the parallel bus cannot carry sends no frame at all, and the server's
	 * worker must still see its next command (a Stop) -- -EAGAIN after ~200 ms without a frame */
	{
		unsigned int waited = 0;
		for (;;) {
			mutex_lock(&csi->capture.lock);
			r = vb2_dqbuf(&csi->capture.queue, &b, true);
			mutex_unlock(&csi->capture.lock);
			if (r != -EAGAIN || waited >= 200)
				break;
			msleep(2);
			waited += 2;
		}
	}
	if (r == -EAGAIN)
		return r;
	if (r != 0) {
		shim_log("camera-a64: next_frame: dqbuf -> %d", r);
		return r;
	}
	vb = vb2_get_buffer(&csi->capture.queue, b.index);
	n = min(len, b.bytesused);
	memcpy(dst, vb2_plane_vaddr(vb, 0), n);
	r = locked_qbuf(&b);
	return r ? r : (int)n;
}

static void release_hold(void)
{
	if (held) {
		pm_runtime_put(held);
		held = NULL;
	}
}

/* keep_power: keep the sensor powered until the next start (or a plain stop), as Linux's 1 s runtime-PM
 * autosuspend does across Megapixels' viewfinder -> still mode switch; our shim suspends at once, and an
 * OV5640 powered up again ~100 ms after power-off NAKs its first write (reg 3008) */
int phe_camera_stop(int keep_power)
{
	struct v4l2_requestbuffers req = { .count = 0, .type = V4L2_BUF_TYPE_VIDEO_CAPTURE, .memory = V4L2_MEMORY_MMAP };
	if (!keep_power)
		release_hold();
	if (!streaming)
		return 0;
	if (keep_power && !held && cur_sensor) {
		struct i2c_client *c = v4l2_get_subdevdata(cur_sensor);
		if (c && pm_runtime_get_sync(&c->dev) >= 0)
			held = &c->dev;
	}
	streaming = 0;
	mutex_lock(&csi->capture.lock);
	vb2_streamoff(&csi->capture.queue, V4L2_BUF_TYPE_VIDEO_CAPTURE);
	mutex_unlock(&csi->capture.lock);
	return locked_reqbufs(&req);
}

int phe_camera_rotation(int unit)
{
	struct v4l2_subdev *sd = csi->bridge.source_parallel[unit].subdev;
	struct v4l2_ctrl *c = sd ? v4l2_ctrl_find(sd->ctrl_handler, V4L2_CID_CAMERA_SENSOR_ROTATION) : NULL;
	return c ? v4l2_ctrl_g_ctrl(c) : 0;
}

int phe_camera_ctrl(int unit, uint32_t id, int32_t value)
{
	struct v4l2_subdev *sd = csi->bridge.source_parallel[unit].subdev;
	struct v4l2_ctrl *c = sd ? v4l2_ctrl_find(sd->ctrl_handler, id) : NULL;
	return c ? v4l2_ctrl_s_ctrl(c, value) : -EINVAL;
}

/* the sensor's frame sizes for a media-bus code (VIDIOC_SUBDEV_ENUM_FRAME_SIZE on its pad 0), as media-ctl lists them */
int phe_camera_enum_size(int unit, unsigned int index, uint32_t mbus_code, unsigned int *width, unsigned int *height)
{
	struct v4l2_subdev *sd = csi->bridge.source_parallel[unit].subdev;
	struct v4l2_subdev_frame_size_enum fse = { .index = index, .pad = 0, .code = mbus_code,
		.which = V4L2_SUBDEV_FORMAT_ACTIVE };
	struct v4l2_subdev_state *state;
	int r;
	if (!sd)
		return -ENODEV;
	state = v4l2_subdev_lock_and_get_active_state(sd);
	r = v4l2_subdev_call(sd, pad, enum_frame_size, state, &fse);
	if (state)
		v4l2_subdev_unlock_state(state);
	if (r)
		return r;
	*width = fse.max_width;
	*height = fse.max_height;
	return 0;
}

int phe_camera_get_ctrl(int unit, uint32_t id, int32_t *value)
{
	struct v4l2_subdev *sd = csi->bridge.source_parallel[unit].subdev;
	struct v4l2_ctrl *c = sd ? v4l2_ctrl_find(sd->ctrl_handler, id) : NULL;
	if (!c)
		return -EINVAL;
	*value = v4l2_ctrl_g_ctrl(c);
	return 0;
}

/* VIDIOC_QUERYCTRL (+ the menu skip mask VIDIOC_QUERYMENU walks) */
int phe_camera_query_ctrl(int unit, uint32_t id, struct phe_camera_ctrl_info *info)
{
	struct v4l2_subdev *sd = csi->bridge.source_parallel[unit].subdev;
	struct v4l2_ctrl *c = sd ? v4l2_ctrl_find(sd->ctrl_handler, id) : NULL;
	if (!c)
		return -EINVAL;
	info->type = c->type;
	info->minimum = (int32_t)c->minimum;
	info->maximum = (int32_t)c->maximum;
	info->step = (int32_t)c->step;
	info->def = (int32_t)c->default_value;
	info->menu_skip_mask = (uint32_t)c->menu_skip_mask;
	info->flags = c->flags;
	return 0;
}

/* an integer menu's item value (VIDIOC_QUERYMENU .value), e.g. V4L2_CID_AUTO_EXPOSURE_BIAS in milli-EV */
int phe_camera_menu_value(int unit, uint32_t id, unsigned int index, int64_t *value)
{
	struct v4l2_subdev *sd = csi->bridge.source_parallel[unit].subdev;
	struct v4l2_ctrl *c = sd ? v4l2_ctrl_find(sd->ctrl_handler, id) : NULL;
	if (!c || c->type != V4L2_CTRL_TYPE_INTEGER_MENU || index > c->maximum || (c->menu_skip_mask & BIT_ULL(index)))
		return -EINVAL;
	*value = c->qmenu_int[index];
	return 0;
}
