/* Copyright The Phorepoc Authors
 * SPDX-License-Identifier: GPL-2.0-only */

/* phe_rt.c -- the shim runtime in portable C: the Linux device model, runtime PM, platform and I2C cores, clocks,
 * resets, GPIO descriptors, regulators, IRQ requests, coherent DMA, the MMIO regmap and firmware requests, each
 * resolved against the static device tree (phe_fwnode.h) and the hardware calls of glue/phe_hw.h. Paths the camera
 * does not take on Symbian (char-device nodes, sysfs, dma-buf, user-page pinning, mmap, kthreads, request fds)
 * succeed as no-ops or fail with -ENOSYS; nothing in them is reached by the glue. */
#include "../include/phe_fwnode.h"
#include "../../os/src/phecamserver/phe_hw.h"

/* ---- logging, names ---- */
void phe_dev_log(const struct device *dev, const char *level, const char *fmt, ...)
{
	char buf[256];
	va_list ap;
	va_start(ap, fmt);
	shim_vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	shim_log("camera %s %s: %s", level, dev ? dev_name(dev) : "-", buf);
}

int dev_err_probe(const struct device *dev, int err, const char *fmt, ...)
{
	char buf[256];
	va_list ap;
	va_start(ap, fmt);
	shim_vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	if (err != -EPROBE_DEFER)
		phe_dev_log(dev, "E", "error %d: %s", err, buf);
	return err;
}

const char *dev_name(const struct device *dev)
{
	if (dev->init_name)
		return dev->init_name;
	if (dev->fwnode)
		return fwnode_get_name(dev->fwnode);
	return "?";
}

int dev_set_name(struct device *dev, const char *fmt, ...)
{
	char *s = shim_alloc(32);
	va_list ap;
	if (!s)
		return -ENOMEM;
	va_start(ap, fmt);
	shim_vsnprintf(s, 32, fmt, ap);
	va_end(ap);
	dev->init_name = s;
	return 0;
}

int kobject_set_name(void *kobj, const char *fmt, ...) { (void)kobj; (void)fmt; return 0; }

ssize_t phe_strscpy(char *dst, const char *src, size_t size)
{
	size_t i;
	if (!size)
		return -E2BIG;
	for (i = 0; i + 1 < size && src[i]; ++i)
		dst[i] = src[i];
	dst[i] = 0;
	return src[i] ? -E2BIG : (ssize_t)i;
}

int phe_kstrtou16(const char *s, unsigned int base, u16 *res)
{
	u32 v = 0;
	unsigned int d;
	if (!base)
		base = 10;
	if (!*s)
		return -EINVAL;
	for (; *s && *s != '\n'; ++s) {
		if (*s >= '0' && *s <= '9') d = *s - '0';
		else if ((*s | 0x20) >= 'a' && (*s | 0x20) <= 'f') d = (*s | 0x20) - 'a' + 10;
		else return -EINVAL;
		if (d >= base)
			return -EINVAL;
		v = v * base + d;
		if (v > 0xffff)
			return -ERANGE;
	}
	*res = (u16)v;
	return 0;
}

void *phe_krealloc(void *p, size_t n) { return shim_realloc(p, n); }	/* media_pipeline_start grows its walk stack */

unsigned long bitmap_find_next_zero_area(unsigned long *map, unsigned long size, unsigned long start,
	unsigned int nr, unsigned long align_mask)
{
	unsigned long i, j;
	for (i = (start + align_mask) & ~align_mask; i + nr <= size; i = ((i + 1) + align_mask) & ~align_mask) {
		for (j = 0; j < nr && !test_bit(i + j, map); ++j)
			;
		if (j == nr)
			return i;
	}
	return size;
}

/* ---- device model: registration is bookkeeping only (no sysfs, no char-device nodes) ---- */
struct device *get_device(struct device *dev) { return dev; }
void put_device(struct device *dev) { (void)dev; }
void device_initialize(struct device *dev) { (void)dev; }
int device_add(struct device *dev) { (void)dev; return 0; }
void device_del(struct device *dev) { (void)dev; }
int device_register(struct device *dev) { return device_add(dev); }
void device_unregister(struct device *dev) { if (dev->release) dev->release(dev); }
int device_create_file(struct device *dev, const struct device_attribute *a) { (void)dev; (void)a; return 0; }
void device_remove_file(struct device *dev, const struct device_attribute *a) { (void)dev; (void)a; }
int class_register(struct class *c) { (void)c; return 0; }
void class_unregister(struct class *c) { (void)c; }
int bus_register(const struct bus_type *b) { (void)b; return 0; }
void bus_unregister(const struct bus_type *b) { (void)b; }
int register_chrdev_region(dev_t from, unsigned count, const char *name) { (void)from; (void)count; (void)name; return 0; }
int alloc_chrdev_region(dev_t *dev, unsigned baseminor, unsigned count, const char *name)
{ (void)count; (void)name; *dev = MKDEV(81, baseminor); return 0; }
void unregister_chrdev_region(dev_t from, unsigned count) { (void)from; (void)count; }
void cdev_init(struct cdev *c, const struct file_operations *fops) { c->ops = fops; }
struct cdev *cdev_alloc(void) { return shim_zalloc(sizeof(struct cdev)); }
int cdev_add(struct cdev *c, dev_t d, unsigned count) { (void)c; (void)d; (void)count; return 0; }
int cdev_device_add(struct cdev *c, struct device *dev) { (void)c; (void)dev; return 0; }
void cdev_device_del(struct cdev *c, struct device *dev) { (void)c; (void)dev; }
void cdev_del(struct cdev *c) { (void)c; }

/* ---- runtime PM: a usage count; 0 -> 1 resumes, 1 -> 0 suspends, synchronously ---- */
static const struct dev_pm_ops *pm_ops(struct device *dev) { return dev->driver ? dev->driver->pm : NULL; }
static int pm_resume(struct device *dev)
{
	const struct dev_pm_ops *o = pm_ops(dev);
	int r = 0;
	if (!dev->phe_pm_active && o && o->runtime_resume)
		r = o->runtime_resume(dev);
	if (!r)
		dev->phe_pm_active = 1;
	return r;
}
static int pm_suspend(struct device *dev)
{
	const struct dev_pm_ops *o = pm_ops(dev);
	int r = 0;
	if (dev->phe_pm_active && dev->phe_pm_enabled && o && o->runtime_suspend)
		r = o->runtime_suspend(dev);
	if (!r && dev->phe_pm_enabled)
		dev->phe_pm_active = 0;
	return r;
}
int pm_runtime_resume_and_get(struct device *dev)
{
	int r = pm_resume(dev);
	if (r)
		return r;
	dev->phe_pm_usage++;
	return 0;
}
int pm_runtime_get_sync(struct device *dev) { dev->phe_pm_usage++; return pm_resume(dev); }
int pm_runtime_get_if_in_use(struct device *dev)
{
	if (!dev->phe_pm_enabled)
		return -EINVAL;
	if (!dev->phe_pm_active || !dev->phe_pm_usage)
		return 0;
	dev->phe_pm_usage++;
	return 1;
}
void pm_runtime_get_noresume(struct device *dev) { dev->phe_pm_usage++; }
int pm_runtime_put(struct device *dev)
{
	if (dev->phe_pm_usage > 0 && --dev->phe_pm_usage == 0)
		return pm_suspend(dev);
	return 0;
}
int pm_runtime_put_sync(struct device *dev) { return pm_runtime_put(dev); }
int pm_runtime_put_autosuspend(struct device *dev) { return pm_runtime_put(dev); }
int pm_runtime_put_noidle(struct device *dev) { if (dev->phe_pm_usage > 0) dev->phe_pm_usage--; return 0; }
int pm_runtime_idle(struct device *dev) { return dev->phe_pm_usage ? 0 : pm_suspend(dev); }
void pm_runtime_enable(struct device *dev) { dev->phe_pm_enabled = 1; }
void pm_runtime_disable(struct device *dev) { dev->phe_pm_enabled = 0; }
int pm_runtime_set_active(struct device *dev) { dev->phe_pm_active = 1; return 0; }
void pm_runtime_set_suspended(struct device *dev) { dev->phe_pm_active = 0; }
bool pm_runtime_status_suspended(struct device *dev) { return !dev->phe_pm_active; }
void pm_runtime_use_autosuspend(struct device *dev) { dev->phe_pm_autosuspend = 1; }
void pm_runtime_dont_use_autosuspend(struct device *dev) { dev->phe_pm_autosuspend = 0; }
void pm_runtime_set_autosuspend_delay(struct device *dev, int delay) { (void)dev; (void)delay; }
void pm_runtime_mark_last_busy(struct device *dev) { (void)dev; }
int pm_runtime_force_suspend(struct device *dev) { int r = pm_suspend(dev); return r; }
int pm_runtime_force_resume(struct device *dev) { return dev->phe_pm_usage ? pm_resume(dev) : 0; }

/* ---- the platform bus ---- */
static const struct bus_type phe_platform_bus = { "platform" };
#define MAX_DRV 8
static struct platform_driver *pdrivers[MAX_DRV];
static struct i2c_driver *idrivers[MAX_DRV];
const struct of_device_id *phe_of_match(const struct of_device_id *table, const struct fwnode_handle *f);

int dev_is_platform(const struct device *dev) { return dev->bus == &phe_platform_bus; }

int platform_driver_register(struct platform_driver *drv)
{
	int i;
	for (i = 0; i < MAX_DRV; ++i)
		if (!pdrivers[i]) { pdrivers[i] = drv; return 0; }
	return -ENOSPC;
}
void platform_driver_unregister(struct platform_driver *drv)
{
	int i;
	for (i = 0; i < MAX_DRV; ++i)
		if (pdrivers[i] == drv) pdrivers[i] = NULL;
}

/* glue: bind a platform device (resources filled in by the board file) to the driver its node matches */
int phe_platform_probe(struct platform_device *pdev)
{
	int i, r;
	pdev->dev.bus = &phe_platform_bus;
	for (i = 0; i < MAX_DRV; ++i) {
		struct platform_driver *d = pdrivers[i];
		if (!d || !phe_of_match(d->driver.of_match_table, pdev->dev.fwnode))
			continue;
		pdev->dev.driver = &d->driver;
		r = d->probe(pdev);
		if (r)
			pdev->dev.driver = NULL;
		return r;
	}
	return -ENODEV;
}

int platform_get_irq(struct platform_device *pdev, unsigned int num)
{
	u32 i, k = 0;
	for (i = 0; i < pdev->num_resources; ++i)
		if ((pdev->resource[i].flags & IORESOURCE_IRQ) && k++ == num)
			return (int)pdev->resource[i].start;
	return -ENXIO;
}

void __iomem *devm_platform_ioremap_resource(struct platform_device *pdev, unsigned int index)
{
	u32 i, k = 0;
	for (i = 0; i < pdev->num_resources; ++i) {
		struct resource *r = &pdev->resource[i];
		if ((r->flags & IORESOURCE_MEM) && k++ == index) {
			void *v = phe_hw_map(r->start, r->end - r->start + 1);
			return v ? v : ERR_PTR(-ENOMEM);
		}
	}
	return ERR_PTR(-EINVAL);
}

/* ---- clocks and resets: <&ccu N> entries of "clocks" / "resets", by "clock-names" / "reset-names" ---- */
struct clk { unsigned int id; int on; };
struct reset_control { unsigned int id; };

static int named_index(struct device *dev, const char *list, const char *id)
{
	const struct phe_prop *p = phe_node_prop(PHE_NODE_OF(dev->fwnode), list);
	unsigned int i;
	if (!id)
		return 0;
	for (i = 0; p && i < p->n; ++i)
		if (!shim_strcmp(p->strs[i], id))
			return (int)i;
	return -ENOENT;
}

static int ref_cell(struct device *dev, const char *prop, int index, u32 *cell)
{
	struct fwnode_reference_args a;
	int r;
	if (index < 0)
		return index;
	r = fwnode_property_get_reference_args(dev->fwnode, prop, NULL, 0, (unsigned int)index, &a);
	if (r)
		return r;
	*cell = a.nargs ? (u32)a.args[0] : 0;
	return 0;
}

struct clk *devm_clk_get(struct device *dev, const char *id)
{
	struct clk *c;
	u32 cell;
	if (ref_cell(dev, "clocks", named_index(dev, "clock-names", id), &cell))
		return ERR_PTR(-ENOENT);
	c = shim_zalloc(sizeof(*c));
	if (!c)
		return ERR_PTR(-ENOMEM);
	c->id = cell;
	return c;
}
struct clk *devm_clk_get_optional(struct device *dev, const char *id)
{
	struct clk *c = devm_clk_get(dev, id);
	return IS_ERR(c) ? NULL : c;
}
int clk_prepare_enable(struct clk *c)
{
	int r;
	if (!c || c->on)
		return 0;
	r = phe_hw_clk_enable(c->id, 1);
	if (!r)
		c->on = 1;
	return r;
}
void clk_disable_unprepare(struct clk *c) { if (c && c->on) { phe_hw_clk_enable(c->id, 0); c->on = 0; } }
unsigned long clk_get_rate(struct clk *c) { return c ? phe_hw_clk_get_rate(c->id) : 0; }
int clk_set_rate(struct clk *c, unsigned long rate) { return c ? phe_hw_clk_set_rate(c->id, rate) : 0; }
int clk_set_rate_exclusive(struct clk *c, unsigned long rate) { return clk_set_rate(c, rate); }
void clk_rate_exclusive_put(struct clk *c) { (void)c; }

struct reset_control *devm_reset_control_get_shared(struct device *dev, const char *id)
{
	struct reset_control *rc;
	u32 cell;
	if (ref_cell(dev, "resets", named_index(dev, "reset-names", id), &cell))
		return ERR_PTR(-ENOENT);
	rc = shim_zalloc(sizeof(*rc));
	if (!rc)
		return ERR_PTR(-ENOMEM);
	rc->id = cell;
	return rc;
}
struct reset_control *devm_reset_control_get_exclusive(struct device *dev, const char *id)
{ return devm_reset_control_get_shared(dev, id); }
int reset_control_assert(struct reset_control *rc) { return rc ? phe_hw_reset(rc->id, 1) : 0; }
int reset_control_deassert(struct reset_control *rc) { return rc ? phe_hw_reset(rc->id, 0) : 0; }

/* ---- GPIO descriptors: <&pio bank pin flags>, flags as the DT's GPIO_ACTIVE_LOW (1) / GPIO_OPEN_DRAIN (6) ---- */
struct gpio_desc { unsigned int bank, pin; int active_low, open_drain; };

static struct gpio_desc *gpio_lookup(struct device *dev, const char *con_id, unsigned int idx, enum gpiod_flags flags)
{
	char prop[32];
	struct fwnode_reference_args a;
	struct gpio_desc *d;
	if (con_id)
		shim_snprintf(prop, sizeof(prop), "%s-gpios", con_id);
	else
		phe_strscpy(prop, "gpios", sizeof(prop));
	if (fwnode_property_get_reference_args(dev->fwnode, prop, NULL, 0, idx, &a) || a.nargs < 3)
		return ERR_PTR(-ENOENT);
	d = shim_zalloc(sizeof(*d));
	if (!d)
		return ERR_PTR(-ENOMEM);
	d->bank = (unsigned int)a.args[0];
	d->pin = (unsigned int)a.args[1];
	d->active_low = (a.args[2] & 1) != 0;
	d->open_drain = (a.args[2] & 6) == 6;
	if (flags & 2)		/* GPIOD_FLAGS_BIT_DIR_OUT */
		gpiod_direction_output(d, (flags & 4) != 0);
	else if (flags & 1)
		gpiod_direction_input(d);
	return d;
}

struct gpio_desc *gpiod_get_index(struct device *dev, const char *con_id, unsigned int idx, enum gpiod_flags flags)
{ return gpio_lookup(dev, con_id, idx, flags); }
struct gpio_desc *gpiod_get(struct device *dev, const char *con_id, enum gpiod_flags flags)
{ return gpio_lookup(dev, con_id, 0, flags); }
struct gpio_desc *devm_gpiod_get(struct device *dev, const char *con_id, enum gpiod_flags flags)
{ return gpio_lookup(dev, con_id, 0, flags); }
struct gpio_desc *devm_gpiod_get_optional(struct device *dev, const char *con_id, enum gpiod_flags flags)
{
	struct gpio_desc *d = gpio_lookup(dev, con_id, 0, flags);
	return PTR_ERR(d) == -ENOENT ? NULL : d;
}
void gpiod_put(struct gpio_desc *d) { if (!IS_ERR_OR_NULL(d)) shim_free(d); }
int gpiod_direction_output(struct gpio_desc *d, int value)
{ return d ? phe_hw_gpio_dir(d->bank, d->pin, 1, value ^ d->active_low, d->open_drain) : 0; }
int gpiod_direction_input(struct gpio_desc *d) { return d ? phe_hw_gpio_dir(d->bank, d->pin, 0, 0, 0) : 0; }
void gpiod_set_value(struct gpio_desc *d, int v) { if (d) phe_hw_gpio_set(d->bank, d->pin, !!v ^ d->active_low); }
void gpiod_set_value_cansleep(struct gpio_desc *d, int v) { gpiod_set_value(d, v); }
int gpiod_get_value(const struct gpio_desc *d) { return d ? phe_hw_gpio_get(d->bank, d->pin) ^ d->active_low : 0; }
int gpiod_get_value_cansleep(const struct gpio_desc *d) { return gpiod_get_value(d); }
int gpiod_cansleep(const struct gpio_desc *d) { (void)d; return 0; }
int gpiod_to_irq(const struct gpio_desc *d) { (void)d; return -ENXIO; }
int desc_to_gpio(const struct gpio_desc *d) { return d ? (int)(d->bank * 32 + d->pin) : -1; }

/* ---- regulators: "<supply>-supply" references to rail nodes, counted per rail (rails are shared) ---- */
struct regulator { const char *rail; int users; };
#define MAX_RAILS 8
static struct regulator rails[MAX_RAILS];

static struct regulator *rail_get(const char *name)
{
	int i;
	for (i = 0; i < MAX_RAILS; ++i)
		if (rails[i].rail && !shim_strcmp(rails[i].rail, name))
			return &rails[i];
	for (i = 0; i < MAX_RAILS; ++i)
		if (!rails[i].rail) { rails[i].rail = name; return &rails[i]; }
	return NULL;
}

int devm_regulator_bulk_get(struct device *dev, int num, struct regulator_bulk_data *c)
{
	char prop[40];
	struct fwnode_reference_args a;
	int i;
	for (i = 0; i < num; ++i) {
		shim_snprintf(prop, sizeof(prop), "%s-supply", c[i].supply);
		if (fwnode_property_get_reference_args(dev->fwnode, prop, NULL, 0, 0, &a))
			return dev_err_probe(dev, -ENODEV, "no %s", prop);
		c[i].consumer = rail_get(fwnode_get_name(a.fwnode));
		if (!c[i].consumer)
			return -ENOMEM;
	}
	return 0;
}

int regulator_bulk_enable(int num, struct regulator_bulk_data *c)
{
	int i, r;
	for (i = 0; i < num; ++i) {
		struct regulator *g = c[i].consumer;
		if (g->users++ == 0 && (r = phe_hw_rail(g->rail, 1)) != 0) {
			g->users--;
			while (--i >= 0)
				if (--c[i].consumer->users == 0)
					phe_hw_rail(c[i].consumer->rail, 0);
			return r;
		}
	}
	return 0;
}

int regulator_bulk_disable(int num, struct regulator_bulk_data *c)
{
	int i;
	for (i = num - 1; i >= 0; --i)
		if (c[i].consumer->users > 0 && --c[i].consumer->users == 0)
			phe_hw_rail(c[i].consumer->rail, 0);
	return 0;
}

/* ---- interrupts ---- */
struct phe_irq { irq_handler_t fn; void *ctx; };
static int irq_tramp(int irq, void *ctx)
{
	struct phe_irq *h = ctx;
	return h->fn(irq, h->ctx) == IRQ_HANDLED;
}
int request_irq(unsigned int irq, irq_handler_t handler, unsigned long flags, const char *name, void *dev_id)
{
	struct phe_irq *h = shim_zalloc(sizeof(*h));
	(void)flags; (void)name;
	if (!h)
		return -ENOMEM;
	h->fn = handler;
	h->ctx = dev_id;
	return phe_hw_irq_bind((int)irq, irq_tramp, h);
}
int devm_request_irq(struct device *dev, unsigned int irq, irq_handler_t handler, unsigned long flags,
	const char *name, void *dev_id)
{
	(void)dev;
	return request_irq(irq, handler, flags, name, dev_id);
}
void enable_irq(unsigned int irq) { phe_hw_irq_enable((int)irq, 1); }
void disable_irq(unsigned int irq) { phe_hw_irq_enable((int)irq, 0); }

/* ---- coherent DMA (vb2-dma-contig's allocator) ---- */
void *dma_alloc_attrs(struct device *dev, size_t size, dma_addr_t *dma_handle, gfp_t flag, unsigned long attrs)
{
	uint32_t phys = 0;
	void *v;
	(void)dev; (void)flag; (void)attrs;
	v = phe_hw_dma_alloc(size, &phys);
	if (v)
		*dma_handle = phys;
	return v;
}
void dma_free_attrs(struct device *dev, size_t size, void *cpu_addr, dma_addr_t dma_handle, unsigned long attrs)
{ (void)dev; (void)attrs; phe_hw_dma_free(cpu_addr, size, dma_handle); }
void *dma_alloc_coherent(struct device *dev, size_t size, dma_addr_t *h, gfp_t gfp) { return dma_alloc_attrs(dev, size, h, gfp, 0); }
void dma_free_coherent(struct device *dev, size_t size, void *v, dma_addr_t h) { dma_free_attrs(dev, size, v, h, 0); }
int dma_mapping_error(struct device *dev, dma_addr_t a) { (void)dev; return a == DMA_MAPPING_ERROR; }
unsigned int dma_get_max_seg_size(struct device *dev) { return dev->dma_parms ? dev->dma_parms->max_segment_size : 65536; }
int dma_set_max_seg_size(struct device *dev, unsigned int size)
{
	if (!dev->dma_parms)
		return -EIO;
	dev->dma_parms->max_segment_size = size;
	return 0;
}

/* ---- MMIO regmap (32-bit registers) ---- */
struct regmap { volatile u32 *base; unsigned int max; };
struct regmap *devm_regmap_init_mmio_clk(struct device *dev, const char *clk_id, void __iomem *regs,
	const struct regmap_config *config)
{
	struct regmap *m = shim_zalloc(sizeof(*m));
	(void)dev; (void)clk_id;
	if (!m)
		return ERR_PTR(-ENOMEM);
	m->base = regs;
	m->max = config ? config->max_register : 0;
	return m;
}
int regmap_write(struct regmap *m, unsigned int reg, unsigned int val) { m->base[reg / 4] = val; return 0; }
int regmap_read(struct regmap *m, unsigned int reg, unsigned int *val) { *val = m->base[reg / 4]; return 0; }
int regmap_update_bits(struct regmap *m, unsigned int reg, unsigned int mask, unsigned int val)
{
	u32 v = m->base[reg / 4];
	m->base[reg / 4] = (v & ~mask) | (val & mask);
	return 0;
}

/* ---- I2C core: adapters, clients from the adapter node's children, transfers ---- */
#define MAX_ADAP 4
static struct i2c_adapter *adapters[MAX_ADAP];
static int next_nr;

int phe_i2c_bind(struct i2c_client *c)
{
	int i, r;
	for (i = 0; i < MAX_DRV; ++i) {
		struct i2c_driver *d = idrivers[i];
		if (!d || !phe_of_match(d->driver.of_match_table, c->dev.fwnode))
			continue;
		c->dev.driver = &d->driver;
		r = d->probe(c);
		if (r)
			c->dev.driver = NULL;
		return r;
	}
	return -ENODEV;
}

int phe_i2c_driver_register(struct i2c_driver *drv)
{
	int i;
	for (i = 0; i < MAX_DRV; ++i)
		if (!idrivers[i]) { idrivers[i] = drv; return 0; }
	return -ENOSPC;
}

static struct i2c_client *new_client(struct i2c_adapter *adap, struct fwnode_handle *node, unsigned short addr)
{
	struct i2c_client *c = shim_zalloc(sizeof(*c));
	if (!c)
		return ERR_PTR(-ENOMEM);
	c->adapter = adap;
	c->addr = addr;
	c->dev.parent = &adap->dev;
	device_set_node(&c->dev, node);
	phe_strscpy(c->name, node ? fwnode_get_name(node) : "i2c", sizeof(c->name));
	return c;
}

int i2c_add_adapter(struct i2c_adapter *adap)
{
	struct fwnode_handle *child;
	int i;
	if (!adap->lock_ops)
		rt_mutex_init(&adap->bus_lock);
	for (i = 0; i < MAX_ADAP && adapters[i]; ++i)
		;
	if (i == MAX_ADAP)
		return -ENOSPC;
	adapters[i] = adap;
	if (adap->nr < 0 || !adap->nr)
		adap->nr = next_nr++;
	if (!adap->dev.fwnode)
		return 0;
	/* the clients: every available child with a "reg" (the sensors), probed in tree order. A probe that returns
	 * -EPROBE_DEFER is retried, as the Linux driver core does, after another device has bound (the GC2145 defers
	 * when it cannot be reached; the OV5640 probing next moves itself from the shared 0x3c to 0x4c) */
	{
		struct i2c_client *deferred[MAX_ADAP * 2];
		int nd = 0, progress = 0, r;
		fwnode_for_each_child_node(adap->dev.fwnode, child) {
			u32 addr;
			struct i2c_client *c;
			if (!fwnode_device_is_available(child) || fwnode_property_read_u32(child, "reg", &addr))
				continue;
			c = new_client(adap, child, (unsigned short)addr);
			if (IS_ERR(c))
				return PTR_ERR(c);
			r = phe_i2c_bind(c);
			if (r == -EPROBE_DEFER && nd < (int)(sizeof(deferred) / sizeof(deferred[0])))
				deferred[nd++] = c;
			else if (r)
				shim_free(c);
			else
				progress = 1;
		}
		while (nd && progress) {
			int i, n = nd;
			progress = 0;
			nd = 0;
			for (i = 0; i < n; ++i) {
				r = phe_i2c_bind(deferred[i]);
				shim_log("camera-a64: deferred probe of %s -> %d", deferred[i]->name, r);
				if (r == -EPROBE_DEFER)
					deferred[nd++] = deferred[i];
				else if (r)
					shim_free(deferred[i]);
				else
					progress = 1;
			}
		}
		while (nd)
			shim_free(deferred[--nd]);
	}
	return 0;
}
int i2c_add_numbered_adapter(struct i2c_adapter *adap) { return i2c_add_adapter(adap); }
void i2c_del_adapter(struct i2c_adapter *adap)
{
	int i;
	for (i = 0; i < MAX_ADAP; ++i)
		if (adapters[i] == adap) adapters[i] = NULL;
}

void i2c_lock_bus(struct i2c_adapter *adap, unsigned int flags)
{
	if (adap->lock_ops) adap->lock_ops->lock_bus(adap, flags); else rt_mutex_lock(&adap->bus_lock);
}
void i2c_unlock_bus(struct i2c_adapter *adap, unsigned int flags)
{
	if (adap->lock_ops) adap->lock_ops->unlock_bus(adap, flags); else rt_mutex_unlock(&adap->bus_lock);
}

int __i2c_transfer(struct i2c_adapter *adap, struct i2c_msg *msgs, int num)
{
	int r = -EAGAIN, t;
	if (!adap->algo || !adap->algo->xfer)
		return -EOPNOTSUPP;
	for (t = 0; t <= adap->retries; ++t) {
		r = adap->algo->xfer(adap, msgs, num);
		if (r != -EAGAIN)
			break;
	}
	return r;
}
int i2c_transfer(struct i2c_adapter *adap, struct i2c_msg *msgs, int num)
{
	int r;
	i2c_lock_bus(adap, I2C_LOCK_SEGMENT);
	r = __i2c_transfer(adap, msgs, num);
	i2c_unlock_bus(adap, I2C_LOCK_SEGMENT);
	return r;
}

struct i2c_client *i2c_new_client_device(struct i2c_adapter *adap, struct i2c_board_info const *info)
{
	struct i2c_client *c = new_client(adap, info->fwnode, info->addr);
	if (IS_ERR(c))
		return c;
	phe_strscpy(c->name, info->type, sizeof(c->name));
	c->irq = info->irq;
	phe_i2c_bind(c);
	return c;
}
struct i2c_client *i2c_new_scanned_device(struct i2c_adapter *adap, struct i2c_board_info *info,
	unsigned short const *addr_list, int (*probe)(struct i2c_adapter *adap, unsigned short addr))
{
	(void)adap; (void)info; (void)addr_list; (void)probe;
	return ERR_PTR(-ENODEV);
}
void i2c_unregister_device(struct i2c_client *c)
{
	if (IS_ERR_OR_NULL(c))
		return;
	if (c->dev.driver)
		((struct i2c_driver *)container_of(c->dev.driver, struct i2c_driver, driver))->remove(c);
	shim_free(c);
}

/* ---- firmware files: absent = -ENOENT, and the drivers carry on without (ov5640: fixed focus) ---- */
int phe_request_firmware(const struct firmware **fw, const char *name, struct device *device)
{
	struct firmware *f;
	const uint8_t *data;
	size_t size;
	int r;
	(void)device;
	*fw = NULL;
	r = phe_hw_firmware(name, &data, &size);
	if (r)
		return r;
	f = shim_zalloc(sizeof(*f));
	if (!f) {
		phe_hw_firmware_free(data, size);
		return -ENOMEM;
	}
	f->data = data;
	f->size = size;
	*fw = f;
	return 0;
}
void phe_release_firmware(const struct firmware *fw)
{
	if (!fw)
		return;
	phe_hw_firmware_free(fw->data, fw->size);
	shim_free((void *)fw);
}

/* ---- not on the Symbian paths: buffer export/import, user memory, mmap, threads, request fds ---- */
int sg_alloc_table(struct sg_table *t, unsigned int nents, gfp_t gfp) { (void)t; (void)nents; (void)gfp; return -ENOSYS; }
void sg_free_table(struct sg_table *t) { (void)t; }
int sg_alloc_table_from_pages(struct sg_table *sgt, struct page **pages, unsigned int n_pages, unsigned int offset,
	unsigned long size, gfp_t gfp) { (void)sgt; (void)pages; (void)n_pages; (void)offset; (void)size; (void)gfp; return -ENOSYS; }
int dma_get_sgtable_attrs(struct device *dev, struct sg_table *sgt, void *cpu_addr, dma_addr_t dma_addr,
	size_t size, unsigned long attrs) { (void)dev; (void)sgt; (void)cpu_addr; (void)dma_addr; (void)size; (void)attrs; return -ENOSYS; }
int dma_map_sgtable(struct device *dev, struct sg_table *sgt, enum dma_data_direction dir, unsigned long attrs)
{ (void)dev; (void)sgt; (void)dir; (void)attrs; return -ENOSYS; }
void dma_unmap_sgtable(struct device *dev, struct sg_table *sgt, enum dma_data_direction dir, unsigned long attrs)
{ (void)dev; (void)sgt; (void)dir; (void)attrs; }
void dma_sync_sgtable_for_cpu(struct device *dev, struct sg_table *sgt, enum dma_data_direction dir) { (void)dev; (void)sgt; (void)dir; }
void dma_sync_sgtable_for_device(struct device *dev, struct sg_table *sgt, enum dma_data_direction dir) { (void)dev; (void)sgt; (void)dir; }
int dma_mmap_attrs(struct device *dev, struct vm_area_struct *vma, void *cpu_addr, dma_addr_t dma_addr, size_t size,
	unsigned long attrs) { (void)dev; (void)vma; (void)cpu_addr; (void)dma_addr; (void)size; (void)attrs; return -ENOSYS; }
struct sg_table *dma_alloc_noncontiguous(struct device *dev, size_t size, enum dma_data_direction dir, gfp_t gfp,
	unsigned long attrs) { (void)dev; (void)size; (void)dir; (void)gfp; (void)attrs; return NULL; }
void dma_free_noncontiguous(struct device *dev, size_t size, struct sg_table *sgt, enum dma_data_direction dir)
{ (void)dev; (void)size; (void)sgt; (void)dir; }
void *dma_vmap_noncontiguous(struct device *dev, size_t size, struct sg_table *sgt) { (void)dev; (void)size; (void)sgt; return NULL; }
void dma_vunmap_noncontiguous(struct device *dev, void *vaddr) { (void)dev; (void)vaddr; }
int dma_mmap_noncontiguous(struct device *dev, struct vm_area_struct *vma, size_t size, struct sg_table *sgt)
{ (void)dev; (void)vma; (void)size; (void)sgt; return -ENOSYS; }
dma_addr_t dma_map_resource(struct device *dev, phys_addr_t phys_addr, size_t size, enum dma_data_direction dir,
	unsigned long attrs) { (void)dev; (void)phys_addr; (void)size; (void)dir; (void)attrs; return DMA_MAPPING_ERROR; }
void dma_unmap_resource(struct device *dev, dma_addr_t addr, size_t size, enum dma_data_direction dir, unsigned long attrs)
{ (void)dev; (void)addr; (void)size; (void)dir; (void)attrs; }
struct dma_buf *dma_buf_export(const struct dma_buf_export_info *e) { (void)e; return ERR_PTR(-ENOSYS); }
struct dma_buf *dma_buf_get(int fd) { (void)fd; return ERR_PTR(-ENOSYS); }
void dma_buf_put(struct dma_buf *d) { (void)d; }
int dma_buf_fd(struct dma_buf *d, int flags) { (void)d; (void)flags; return -ENOSYS; }
struct dma_buf_attachment *dma_buf_attach(struct dma_buf *d, struct device *dev) { (void)d; (void)dev; return ERR_PTR(-ENOSYS); }
void dma_buf_detach(struct dma_buf *d, struct dma_buf_attachment *a) { (void)d; (void)a; }
struct sg_table *dma_buf_map_attachment_unlocked(struct dma_buf_attachment *a, enum dma_data_direction dir)
{ (void)a; (void)dir; return ERR_PTR(-ENOSYS); }
void dma_buf_unmap_attachment_unlocked(struct dma_buf_attachment *a, struct sg_table *s, enum dma_data_direction dir)
{ (void)a; (void)s; (void)dir; }
int dma_buf_vmap_unlocked(struct dma_buf *d, struct iosys_map *m) { (void)d; (void)m; return -ENOSYS; }
void dma_buf_vunmap_unlocked(struct dma_buf *d, struct iosys_map *m) { (void)d; (void)m; }
int pin_user_pages_fast(unsigned long start, int nr_pages, unsigned int gup_flags, struct page **pages)
{ (void)start; (void)nr_pages; (void)gup_flags; (void)pages; return -ENOSYS; }
void unpin_user_pages(struct page **pages, unsigned long npages) { (void)pages; (void)npages; }
unsigned long phe_page_to_pfn(struct page *p) { (void)p; return 0; }
struct page *phe_pfn_to_page(unsigned long pfn) { (void)pfn; return NULL; }
struct task_struct *phe_kthread_run(int (*fn)(void *), void *data, const char *name)
{ (void)fn; (void)data; (void)name; return ERR_PTR(-ENOSYS); }
int kthread_stop(struct task_struct *k) { (void)k; return 0; }
bool kthread_should_stop(void) { return true; }
struct fd phe_fdget(int fd) { struct fd f = { NULL }; (void)fd; return f; }
void phe_fdput(struct fd f) { (void)f; }
struct file *anon_inode_getfile(const char *name, const struct file_operations *fops, void *priv, int flags)
{ (void)name; (void)fops; (void)priv; (void)flags; return ERR_PTR(-ENOSYS); }
struct phe_fd_prepare phe_fd_prepare(unsigned int flags, struct file *file)
{ struct phe_fd_prepare p = { -ENOSYS, file, -ENOSYS }; (void)flags; return p; }
int phe_fd_publish(struct phe_fd_prepare *p) { return p->err; }
__poll_t poll_requested_events(const poll_table *p) { (void)p; return ~(__poll_t)0; }
