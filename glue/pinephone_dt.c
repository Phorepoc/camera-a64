/* Copyright The Phorepoc Authors
 * SPDX-License-Identifier: GPL-2.0-only */

/* pinephone_dt.c -- the PinePhone camera part of the device tree as a static node table, transcribed from
 * linux/arch/arm64/boot/dts/allwinner/sun50i-a64.dtsi (csi@1cb0000) and sun50i-a64-pinephone.dtsi (&csi, i2c-csi,
 * &i2c_csi: gc2145 front-camera@3c, ov5640 rear-camera@4c), with the same property names and values; the
 * dt-bindings constants are the vendored headers. Then the bring-up the Linux driver core would do: run the media
 * core's initcalls, register the drivers, probe i2c-csi (which adds the adapter and probes the two sensors) and the
 * CSI (whose async notifier binds them). */
#include "../shim/include/phe_fwnode.h"
#include <dt-bindings/clock/sun50i-a64-ccu.h>
#include <dt-bindings/reset/sun50i-a64-ccu.h>
#include <dt-bindings/gpio/gpio.h>

#define U32S(...) .kind = PHE_PROP_U32, .n = sizeof((const u32[]){ __VA_ARGS__ }) / 4, .u32s = (const u32[]){ __VA_ARGS__ }
#define STRS(...) .kind = PHE_PROP_STR, .n = sizeof((const char *const[]){ __VA_ARGS__ }) / sizeof(char *), \
	.strs = (const char *const[]){ __VA_ARGS__ }
#define REF(node, c, ...) .kind = PHE_PROP_REF, .ref = &(node), .cells = (c), \
	.n = sizeof((const u32[]){ 0, ##__VA_ARGS__ }) / 4 - 1, .u32s = (const u32[]){ 0, ##__VA_ARGS__ } + 1
#define FLAG .kind = PHE_PROP_FLAG
#define END { NULL }

/* providers (only their names matter: rails are switched by name in glue, clock/reset/pin cells are indices) */
static struct phe_node ccu = { .name = "clock@1c20000" };
static struct phe_node pio = { .name = "pinctrl@1c20800" };
static struct phe_node reg_aldo1 = { .name = "reg_aldo1" };
static struct phe_node reg_dldo3 = { .name = "reg_dldo3" };
static struct phe_node reg_eldo3 = { .name = "reg_eldo3" };

static struct phe_node csi_ov5640_ep, csi_gc2145_ep, ov5640_ep, gc2145_ep;

/* &csi { port { endpoint@0 (ov5640), endpoint@1 (gc2145) } } */
static const struct phe_prop csi_ov5640_ep_props[] = {
	{ "reg", U32S(0) }, { "remote-endpoint", REF(ov5640_ep, 0) }, { "bus-width", U32S(8) },
	{ "hsync-active", U32S(1) }, { "vsync-active", U32S(0) }, { "data-active", U32S(1) }, { "pclk-sample", U32S(1) }, END };
static const struct phe_prop csi_gc2145_ep_props[] = {
	{ "reg", U32S(1) }, { "remote-endpoint", REF(gc2145_ep, 0) }, { "bus-width", U32S(8) },
	{ "hsync-active", U32S(1) }, { "vsync-active", U32S(1) }, { "data-active", U32S(1) }, { "pclk-sample", U32S(1) }, END };
static struct phe_node csi_ov5640_ep = { .name = "endpoint@0", .props = csi_ov5640_ep_props };
static struct phe_node csi_gc2145_ep = { .name = "endpoint@1", .props = csi_gc2145_ep_props };
static const struct phe_prop port_props[] = { { "#address-cells", U32S(1) }, { "#size-cells", U32S(0) }, END };
static struct phe_node csi_port = { .name = "port", .props = port_props,
	.children = (struct phe_node *const[]){ &csi_ov5640_ep, &csi_gc2145_ep, NULL } };
static const struct phe_prop csi_props[] = {
	{ "compatible", STRS("allwinner,sun50i-a64-csi") },
	{ "reg", U32S(0x01cb0000, 0x1000) },
	{ "interrupts", U32S(0 /* GIC_SPI */, 84, 4 /* IRQ_TYPE_LEVEL_HIGH */) },
	{ "clocks", REF(ccu, 1, CLK_BUS_CSI, CLK_CSI_SCLK, CLK_DRAM_CSI) },
	{ "clock-names", STRS("bus", "mod", "ram") },
	{ "resets", REF(ccu, 1, RST_BUS_CSI) },
	{ "status", STRS("okay") },
	END };
static struct phe_node csi = { .name = "csi@1cb0000", .props = csi_props,
	.children = (struct phe_node *const[]){ &csi_port, NULL } };

/* &i2c_csi { gc2145: front-camera@3c, ov5640: rear-camera@4c } */
static const struct phe_prop gc2145_ep_props[] = {
	{ "remote-endpoint", REF(csi_gc2145_ep, 0) }, { "bus-width", U32S(8) }, { "hsync-active", U32S(1) },
	{ "vsync-active", U32S(1) }, { "data-active", U32S(1) }, { "pclk-sample", U32S(1) }, END };
static struct phe_node gc2145_ep = { .name = "endpoint", .props = gc2145_ep_props };
static struct phe_node gc2145_port = { .name = "port", .children = (struct phe_node *const[]){ &gc2145_ep, NULL } };
static const struct phe_prop gc2145_props[] = {
	{ "compatible", STRS("galaxycore,gc2145") },
	{ "reg", U32S(0x3c) },
	{ "clocks", REF(ccu, 1, CLK_CSI_MCLK) },
	{ "clock-names", STRS("xclk") },
	{ "AVDD-supply", REF(reg_dldo3, 0) },
	{ "DVDD-supply", REF(reg_aldo1, 0) },
	{ "IOVDD-supply", REF(reg_eldo3, 0) },
	{ "reset-gpios", REF(pio, 3, 4, 16, GPIO_ACTIVE_LOW | GPIO_OPEN_DRAIN) },	/* PE16 */
	{ "enable-gpios", REF(pio, 3, 4, 17, GPIO_ACTIVE_LOW | GPIO_OPEN_DRAIN) },	/* PE17 */
	{ "orientation", U32S(0) },
	{ "rotation", U32S(90) },
	END };
static struct phe_node gc2145 = { .name = "front-camera@3c", .props = gc2145_props,
	.children = (struct phe_node *const[]){ &gc2145_port, NULL } };

static const struct phe_prop ov5640_ep_props[] = {
	{ "remote-endpoint", REF(csi_ov5640_ep, 0) }, { "bus-width", U32S(8) }, { "hsync-active", U32S(1) },
	{ "vsync-active", U32S(0) }, { "data-active", U32S(1) }, { "pclk-sample", U32S(1) }, END };
static struct phe_node ov5640_ep = { .name = "endpoint", .props = ov5640_ep_props };
static struct phe_node ov5640_port = { .name = "port", .children = (struct phe_node *const[]){ &ov5640_ep, NULL } };
static const struct phe_prop ov5640_props[] = {
	{ "compatible", STRS("ovti,ov5640") },
	{ "reg", U32S(0x4c) },
	{ "clocks", REF(ccu, 1, CLK_CSI_MCLK) },
	{ "clock-names", STRS("xclk") },
	{ "AVDD-supply", REF(reg_dldo3, 0) },
	{ "DOVDD-supply", REF(reg_aldo1, 0) },
	{ "DVDD-supply", REF(reg_eldo3, 0) },
	{ "reset-gpios", REF(pio, 3, 3, 3, GPIO_ACTIVE_LOW | GPIO_OPEN_DRAIN) },	/* PD3 */
	{ "powerdown-gpios", REF(pio, 3, 2, 0, GPIO_ACTIVE_HIGH | GPIO_OPEN_DRAIN) },	/* PC0 */
	{ "orientation", U32S(1) },
	{ "rotation", U32S(270) },
	END };
static struct phe_node ov5640 = { .name = "rear-camera@4c", .props = ov5640_props,
	.children = (struct phe_node *const[]){ &ov5640_port, NULL } };

static const struct phe_prop i2c_csi_props[] = {
	{ "compatible", STRS("i2c-gpio") },
	{ "sda-gpios", REF(pio, 3, 4, 13, GPIO_ACTIVE_HIGH | GPIO_OPEN_DRAIN) },	/* PE13 */
	{ "scl-gpios", REF(pio, 3, 4, 12, GPIO_ACTIVE_HIGH | GPIO_OPEN_DRAIN) },	/* PE12 */
	{ "i2c-gpio,delay-us", U32S(3) },
	{ "#address-cells", U32S(1) },
	{ "#size-cells", U32S(0) },
	END };
static struct phe_node i2c_csi = { .name = "i2c-csi", .props = i2c_csi_props,
	.children = (struct phe_node *const[]){ &gc2145, &ov5640, NULL } };

static struct phe_node root = { .name = "/",
	.children = (struct phe_node *const[]){ &ccu, &pio, &reg_aldo1, &reg_dldo3, &reg_eldo3, &csi, &i2c_csi, NULL } };

/* ---- bring-up ---- */
/* generated by recipes/build.sh (phe_tables.c): the module_init and module_*_driver entries in link order */
extern const struct phe_driver_entry *const phe_driver_table[];
extern int (*const *const phe_initcall_table[])(void);
int phe_platform_probe(struct platform_device *pdev);
int phe_i2c_driver_register(struct i2c_driver *drv);

static struct resource csi_res[] = {
	{ .start = 0x01cb0000, .end = 0x01cb0fff, .name = "csi", .flags = IORESOURCE_MEM },
	{ .start = 84 + 32, .end = 84 + 32, .name = "csi", .flags = IORESOURCE_IRQ },	/* GIC_SPI 84 */
};
static struct device_dma_parameters csi_dma_parms;
static struct platform_device csi_pdev = { .name = "sun6i-csi", .id = -1, .num_resources = 2, .resource = csi_res };
static struct platform_device i2c_csi_pdev = { .name = "i2c-gpio", .id = -1 };

struct platform_device *phe_camera_csi_pdev(void) { return &csi_pdev; }

int phe_camera_probe_all(void)
{
	const struct phe_driver_entry *const *de;
	int (*const *const *ic)(void);
	int r;

	phe_fwnode_init(&root);
	for (ic = phe_initcall_table; *ic; ++ic)	/* media core, then i2c-gpio's driver */
		if ((r = (**ic)()) != 0)
			return r;
	for (de = phe_driver_table; *de; ++de) {
		const struct phe_driver_entry *e = *de;
		r = e->kind == PHE_DRV_I2C ? phe_i2c_driver_register((struct i2c_driver *)e->drv)
					   : platform_driver_register((struct platform_driver *)e->drv);
		if (r)
			return r;
	}
	device_set_node(&i2c_csi_pdev.dev, &i2c_csi.np.fwnode);
	i2c_csi_pdev.dev.init_name = "i2c-csi";
	if ((r = phe_platform_probe(&i2c_csi_pdev)) != 0)	/* the adapter; its add probes gc2145 and ov5640 */
		return r;
	device_set_node(&csi_pdev.dev, &csi.np.fwnode);
	csi_pdev.dev.init_name = "1cb0000.csi";
	csi_pdev.dev.dma_parms = &csi_dma_parms;
	return phe_platform_probe(&csi_pdev);
}
