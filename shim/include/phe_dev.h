/* Copyright The Phorepoc Authors
 * SPDX-License-Identifier: GPL-2.0-only */

/* phe_dev.h -- the device side of the shim (included by phe_linux.h after phe_core.h): time, kref, ida, wait/poll,
 * files, the device model, firmware nodes, platform devices, MMIO, interrupts, clocks, resets, GPIO descriptors,
 * regulators, runtime PM, coherent DMA, MMIO regmap, I2C, firmware files, debugfs, ioctl encoding. Written from the
 * Linux interface as the vendored files use it (names, types and semantics), not copied from Linux headers.
 *
 * What needs an implementation is a plain function declaration: shim/rt provides the generic ones and glue/ (on the
 * Symbian kernel's Kern::, DPlatChunkHw, GPIO and clock services) the hardware ones. What is still missing is
 * build/arm/camera-a64.undef after recipes/build.sh. */
#ifndef PHE_DEV_H
#define PHE_DEV_H

/* ---- more types and constants ---- */
typedef long long loff_t;
typedef unsigned int __poll_t;
typedef unsigned short umode_t;
typedef unsigned int fmode_t;
typedef u32 phys_addr_t;
typedef u32 resource_size_t;
typedef int pid_t;
#define __counted_by(m)
#define __counted_by_ptr(m)
#define __acquires(x)
#define __releases(x)
#define __must_hold(x)
#define TASK_COMM_LEN 16
#define NSEC_PER_USEC 1000L
#define NSEC_PER_MSEC 1000000L
#define USEC_PER_SEC 1000000L
#define NSEC_PER_SEC 1000000000L
#ifndef EBADR
#define EBADR 53
#endif
#ifndef ENOIOCTLCMD
#define ENOIOCTLCMD 515
#endif
#ifndef EPROBE_DEFER
#define EPROBE_DEFER 517
#endif
#ifndef ENOTSUPP
#define ENOTSUPP 524
#endif
#define cpu_relax() barrier()
#define yield() shim_usleep(0)
#define lockdep_assert_not_held(m) ((void)0)
#define S_IRUGO 0444
#define S_IWUSR 0200
#define S_IRUSR 0400
#define do_div(n, base) ({ u32 __b = (base); u32 __r = (u32)((u64)(n) % __b); (n) = (u64)(n) / __b; __r; })
#ifndef div_u64
#define div_u64(a, b) ((u64)(a) / (u32)(b))
#endif
#define div64_u64(a, b) ((u64)(a) / (u64)(b))
#define div_s64(a, b) ((s64)(a) / (s32)(b))
#define mult_frac(x, n, d) (((x) / (d)) * (n) + (((x) % (d)) * (n)) / (d))
#define str_on_off(v) ((v) ? "on" : "off")
#define str_enabled_disabled(v) ((v) ? "enabled" : "disabled")
#define str_enable_disable(v) ((v) ? "enable" : "disable")
#define str_true_false(v) ((v) ? "true" : "false")


/* ---- time ---- */
struct timespec64 { s64 tv_sec; long tv_nsec; };
static inline struct timespec64 ns_to_timespec64(s64 ns)
{ struct timespec64 t; t.tv_sec = ns / NSEC_PER_SEC; t.tv_nsec = (long)(ns % NSEC_PER_SEC); return t; }
#define ktime_get_ns() ((u64)shim_ktime_ns())
struct __kernel_timespec { s64 tv_sec; long long tv_nsec; };
struct old_timespec32 { s32 tv_sec; s32 tv_nsec; };
struct old_timeval32 { s32 tv_sec; s32 tv_usec; };
struct timeval { long tv_sec; long tv_usec; };
static inline bool bitmap_intersects(const unsigned long *a, const unsigned long *b, unsigned int bits)
{ unsigned int i; for (i = 0; i < BITS_TO_LONGS(bits); ++i) if (a[i] & b[i]) return true; return false; }

/* ---- kref / refcount ---- */
typedef struct { atomic_t refs; } refcount_t;
struct kref { refcount_t refcount; };
#define kref_init(k) ((k)->refcount.refs.counter = 1)
#define kref_get(k) ((void)++(k)->refcount.refs.counter)
#define kref_read(k) ((k)->refcount.refs.counter)
static inline int kref_put(struct kref *k, void (*release)(struct kref *))
{ if (--k->refcount.refs.counter == 0) { release(k); return 1; } return 0; }
#define refcount_set(r, v) ((r)->refs.counter = (v))
#define refcount_read(r) ((r)->refs.counter)
#define refcount_inc(r) ((void)++(r)->refs.counter)
#define refcount_dec_and_test(r) (--(r)->refs.counter == 0)

/* ---- ida (media-entity ids) ---- */
struct ida { unsigned long next; };
#define DEFINE_IDA(n) struct ida n = { 0 }
#define ida_init(i) ((i)->next = 0)
#define ida_destroy(i) ((void)0)
#define ida_alloc_min(i, m, g) ((int)((i)->next < (unsigned long)(m) ? ((i)->next = (m) + 1) - 1 : (i)->next++))
#define ida_free(i, id) ((void)0)

/* ---- wait queues + poll (the v4l2 file ops: declared, our glue drives the subdevs and the vb2 queue directly) ---- */
#define wake_up_interruptible(q) wake_up(q)
#define wake_up_all(q) wake_up(q)
struct poll_table_struct { int unused; };
typedef struct poll_table_struct poll_table;
#define poll_wait(f, q, p) ((void)0)
#define EPOLLIN 0x0001
#define EPOLLPRI 0x0002
#define EPOLLOUT 0x0004
#define EPOLLERR 0x0008
#define EPOLLHUP 0x0010
#define EPOLLRDNORM 0x0040
#define EPOLLWRNORM 0x0100

/* ---- files, chardevs, mm ---- */
struct inode;
struct vm_area_struct;
struct page;
struct module;
struct file_operations;
struct file { void *private_data; fmode_t f_mode; unsigned int f_flags; const struct file_operations *f_op; struct inode *f_inode; };
struct phe_kobject { int unused; };
struct cdev { struct phe_kobject kobj; struct module *owner; const struct file_operations *ops; };
#define O_NONBLOCK 04000
#define FMODE_READ 0x1
#define FMODE_WRITE 0x2

/* ---- device model ---- */
struct fwnode_handle;
struct device_node;
struct device;
struct of_device_id;
struct dev_pm_ops {
	int (*suspend)(struct device *dev);
	int (*resume)(struct device *dev);
	int (*runtime_suspend)(struct device *dev);
	int (*runtime_resume)(struct device *dev);
	int (*runtime_idle)(struct device *dev);
};
struct device_driver { const char *name; struct module *owner; const struct of_device_id *of_match_table; const struct dev_pm_ops *pm;
	const void *acpi_match_table; };
struct bus_type { const char *name; };
struct device_dma_parameters { unsigned int max_segment_size; unsigned int min_align_mask; unsigned long segment_boundary_mask; };
struct attribute_group;
struct class;
struct device {
	struct device *parent;
	const char *init_name;
	struct fwnode_handle *fwnode;
	struct device_node *of_node;
	struct device_driver *driver;
	const struct bus_type *bus;
	void *driver_data;
	void *platform_data;
	u64 *dma_mask;
	u64 coherent_dma_mask;
	void (*release)(struct device *dev);
	u32 devt;
	const struct class *class;
	struct device_dma_parameters *dma_parms;
	const struct attribute_group **groups;
	int phe_pm_usage, phe_pm_active, phe_pm_enabled, phe_pm_autosuspend;	/* runtime PM (shim/rt) */
};
#define dev_get_drvdata(d) ((d)->driver_data)
#define dev_set_drvdata(d, p) ((d)->driver_data = (p))
#define dev_get_platdata(d) ((d)->platform_data)
#define dev_fwnode(d) ((d)->fwnode)
const char *dev_name(const struct device *dev);
int dev_is_platform(const struct device *dev);
#define dev_is_pci(d) 0
void device_set_node(struct device *dev, struct fwnode_handle *fwnode);
struct device *get_device(struct device *dev);
void put_device(struct device *dev);
void phe_dev_log(const struct device *dev, const char *level, const char *fmt, ...) __printf(3, 4);
#define dev_err(d, ...) phe_dev_log(d, "E", __VA_ARGS__)
#define dev_warn(d, ...) phe_dev_log(d, "W", __VA_ARGS__)
#define dev_info(d, ...) phe_dev_log(d, "I", __VA_ARGS__)
#define dev_notice(d, ...) phe_dev_log(d, "N", __VA_ARGS__)
#define dev_dbg(d, ...) do { if (0) phe_dev_log(d, "D", __VA_ARGS__); } while (0)
#define dev_vdbg dev_dbg
#define dev_err_once dev_err
#define dev_warn_once dev_warn
#define dev_err_ratelimited dev_err
#define dev_warn_ratelimited dev_warn
#define dev_dbg_ratelimited dev_dbg
int dev_err_probe(const struct device *dev, int err, const char *fmt, ...) __printf(3, 4);
#ifndef pr_cont
#define pr_cont(...) shim_log(__VA_ARGS__)
#endif
#ifndef pr_notice
#define pr_notice(...) shim_log(__VA_ARGS__)
#endif
void *devm_kmalloc(struct device *dev, size_t size, gfp_t gfp);
#define devm_kcalloc(d, n, sz, g) shim_zalloc((size_t)(n) * (size_t)(sz))
#define devm_kmalloc_array(d, n, sz, g) shim_alloc((size_t)(n) * (size_t)(sz))
int devm_add_action_or_reset(struct device *dev, void (*action)(void *), void *data);

/* ---- firmware nodes / OF / properties (a static node tree from the device tree: glue/) ---- */
struct phe_fwnode;
struct fwnode_handle { const struct phe_fwnode *node; struct fwnode_handle *secondary; struct device *dev; };
struct device_node { struct fwnode_handle fwnode; const char *name; const char *full_name; };
struct fwnode_endpoint { unsigned int port; unsigned int id; const struct fwnode_handle *local_fwnode; };
#define NR_FWNODE_REFERENCE_ARGS 16
struct fwnode_reference_args { struct fwnode_handle *fwnode; unsigned int nargs; u64 args[NR_FWNODE_REFERENCE_ARGS]; };
struct of_device_id { char name[32]; char type[32]; char compatible[128]; const void *data; };
struct acpi_device_id { char id[16]; unsigned long driver_data; };
#define of_fwnode_handle(np) ((np) ? &(np)->fwnode : NULL)
#define to_of_node(f) container_of(f, struct device_node, fwnode)
#define is_of_node(f) 1
#define of_match_ptr(p) (p)
#define ACPI_PTR(p) NULL
#define of_node_put(np) ((void)0)
#define of_node_get(np) (np)
const void *of_device_get_match_data(const struct device *dev);
#define device_get_match_data of_device_get_match_data
struct fwnode_handle *fwnode_handle_get(struct fwnode_handle *fwnode);
void fwnode_handle_put(struct fwnode_handle *fwnode);
bool fwnode_device_is_available(const struct fwnode_handle *fwnode);
struct fwnode_handle *fwnode_get_parent(const struct fwnode_handle *fwnode);
struct fwnode_handle *fwnode_get_next_child_node(const struct fwnode_handle *fwnode, struct fwnode_handle *child);
struct fwnode_handle *fwnode_get_named_child_node(const struct fwnode_handle *fwnode, const char *name);
const char *fwnode_get_name(const struct fwnode_handle *fwnode);
bool fwnode_property_present(const struct fwnode_handle *fwnode, const char *propname);
int fwnode_property_read_u32_array(const struct fwnode_handle *fwnode, const char *propname, u32 *val, size_t nval);
int fwnode_property_read_u64_array(const struct fwnode_handle *fwnode, const char *propname, u64 *val, size_t nval);
int fwnode_property_read_string(const struct fwnode_handle *fwnode, const char *propname, const char **val);
int fwnode_property_get_reference_args(const struct fwnode_handle *fwnode, const char *prop, const char *nargs_prop,
	unsigned int nargs, unsigned int index, struct fwnode_reference_args *args);
#define fwnode_property_read_u32(f, p, v) fwnode_property_read_u32_array(f, p, v, 1)
#define fwnode_property_count_u64(f, p) fwnode_property_read_u64_array(f, p, NULL, 0)
#define fwnode_property_count_u32(f, p) fwnode_property_read_u32_array(f, p, NULL, 0)
#define fwnode_property_read_bool(f, p) fwnode_property_present(f, p)
#define device_property_present(d, p) fwnode_property_present(dev_fwnode(d), p)
#define device_property_read_bool(d, p) fwnode_property_present(dev_fwnode(d), p)
#define device_property_read_u32(d, p, v) fwnode_property_read_u32_array(dev_fwnode(d), p, v, 1)
#define device_property_read_u32_array(d, p, v, n) fwnode_property_read_u32_array(dev_fwnode(d), p, v, n)
#define device_property_read_string(d, p, v) fwnode_property_read_string(dev_fwnode(d), p, v)
struct fwnode_handle *fwnode_graph_get_next_endpoint(const struct fwnode_handle *fwnode, struct fwnode_handle *prev);
struct fwnode_handle *fwnode_graph_get_port_parent(const struct fwnode_handle *fwnode);
struct fwnode_handle *fwnode_graph_get_remote_endpoint(const struct fwnode_handle *fwnode);
struct fwnode_handle *fwnode_graph_get_remote_port_parent(const struct fwnode_handle *fwnode);
struct fwnode_handle *fwnode_graph_get_endpoint_by_id(const struct fwnode_handle *fwnode, u32 port, u32 endpoint,
	unsigned long flags);
int fwnode_graph_parse_endpoint(const struct fwnode_handle *fwnode, struct fwnode_endpoint *endpoint);
#define FWNODE_GRAPH_ENDPOINT_NEXT 0x1
#define FWNODE_GRAPH_DEVICE_DISABLED 0x2
#define fwnode_graph_for_each_endpoint(f, child) \
	for (child = fwnode_graph_get_next_endpoint(f, NULL); child; child = fwnode_graph_get_next_endpoint(f, child))

/* ---- platform devices ---- */
struct resource { resource_size_t start, end; const char *name; unsigned long flags; };
#define IORESOURCE_MEM 0x200
#define IORESOURCE_IRQ 0x400
struct platform_device_id { char name[20]; unsigned long driver_data; };
struct platform_device {
	const char *name;
	int id;
	struct device dev;
	u32 num_resources;
	struct resource *resource;
	const struct platform_device_id *id_entry;
};
struct platform_driver {
	int (*probe)(struct platform_device *);
	void (*remove)(struct platform_device *);
	struct device_driver driver;
	const struct platform_device_id *id_table;
};
#define to_platform_device(d) container_of(d, struct platform_device, dev)
#define platform_get_drvdata(p) dev_get_drvdata(&(p)->dev)
#define platform_set_drvdata(p, d) dev_set_drvdata(&(p)->dev, d)
int platform_get_irq(struct platform_device *pdev, unsigned int num);
void __iomem *devm_platform_ioremap_resource(struct platform_device *pdev, unsigned int index);
int platform_driver_register(struct platform_driver *drv);
void platform_driver_unregister(struct platform_driver *drv);
/* module_*_driver / module_init: a table the glue walks instead of initcalls (section phe_drivers / phe_initcalls) */
struct phe_driver_entry { int kind; const void *drv; };
#define PHE_DRV_PLATFORM 1
#define PHE_DRV_I2C 2
/* not const: a writable section lands beside .data, inside the image's data range (a read-only orphan section
 * sits between .text and .data, where the E32 image has no segment for it) */
#define phe_register_driver(kind, d) \
	static struct phe_driver_entry __phe_drv_##d __attribute__((used, section("phe_drivers"))) = { kind, &(d) }
#define module_platform_driver(d) phe_register_driver(PHE_DRV_PLATFORM, d)
#define module_i2c_driver(d) phe_register_driver(PHE_DRV_I2C, d)
#undef module_init
#undef module_exit
#define module_init(f) static int (*__phe_init_##f)(void) __attribute__((used, section("phe_initcalls"))) = f
#define module_exit(f) static void (*__phe_exit_##f)(void) __attribute__((used)) = f
#define subsys_initcall(f) module_init(f)
#define MODULE_ALIAS(x)
#define MODULE_SOFTDEP(x)
#ifndef module_param
#define module_param(n, t, p)
#endif

/* ---- I/O ---- */
#ifndef readl
#define readl(a) (*(const volatile u32 *)(a))
#define writel(v, a) (*(volatile u32 *)(a) = (v))
#endif

/* ---- interrupts ---- */
typedef enum irqreturn { IRQ_NONE = 0, IRQ_HANDLED = 1, IRQ_WAKE_THREAD = 2 } irqreturn_t;
typedef irqreturn_t (*irq_handler_t)(int, void *);
#define IRQF_SHARED 0x80
#define IRQF_ONESHOT 0x2000
#define IRQF_TRIGGER_RISING 0x1
#define IRQF_TRIGGER_FALLING 0x2
#define IRQF_NO_AUTOEN 0x80000
int devm_request_irq(struct device *dev, unsigned int irq, irq_handler_t handler, unsigned long flags,
	const char *name, void *dev_id);
int request_irq(unsigned int irq, irq_handler_t handler, unsigned long flags, const char *name, void *dev_id);
void free_irq(unsigned int irq, void *dev_id);
void enable_irq(unsigned int irq);
void disable_irq(unsigned int irq);

/* ---- clocks ---- */
struct clk;
struct clk_hw;
struct clk_init_data;
struct clk *devm_clk_get(struct device *dev, const char *id);
struct clk *devm_clk_get_optional(struct device *dev, const char *id);
int clk_prepare_enable(struct clk *clk);
void clk_disable_unprepare(struct clk *clk);
unsigned long clk_get_rate(struct clk *clk);
int clk_set_rate(struct clk *clk, unsigned long rate);
int clk_set_rate_exclusive(struct clk *clk, unsigned long rate);
void clk_rate_exclusive_put(struct clk *clk);
long clk_round_rate(struct clk *clk, unsigned long rate);

/* ---- resets ---- */
struct reset_control;
struct reset_control *devm_reset_control_get_shared(struct device *dev, const char *id);
struct reset_control *devm_reset_control_get_exclusive(struct device *dev, const char *id);
int reset_control_assert(struct reset_control *rstc);
int reset_control_deassert(struct reset_control *rstc);

/* ---- GPIO descriptors ---- */
struct gpio_desc;
enum gpiod_flags { GPIOD_ASIS = 0, GPIOD_IN = 1, GPIOD_OUT_LOW = 3, GPIOD_OUT_HIGH = 7, GPIOD_OUT_LOW_OPEN_DRAIN = 11,
	GPIOD_OUT_HIGH_OPEN_DRAIN = 15 };
struct gpio_desc *devm_gpiod_get(struct device *dev, const char *con_id, enum gpiod_flags flags);
struct gpio_desc *devm_gpiod_get_optional(struct device *dev, const char *con_id, enum gpiod_flags flags);
struct gpio_desc *gpiod_get(struct device *dev, const char *con_id, enum gpiod_flags flags);
struct gpio_desc *gpiod_get_index(struct device *dev, const char *con_id, unsigned int idx, enum gpiod_flags flags);
void gpiod_put(struct gpio_desc *desc);
void gpiod_set_value(struct gpio_desc *desc, int value);
void gpiod_set_value_cansleep(struct gpio_desc *desc, int value);
int gpiod_get_value(const struct gpio_desc *desc);
int gpiod_get_value_cansleep(const struct gpio_desc *desc);
int gpiod_direction_input(struct gpio_desc *desc);
int gpiod_direction_output(struct gpio_desc *desc, int value);
int gpiod_cansleep(const struct gpio_desc *desc);
int gpiod_to_irq(const struct gpio_desc *desc);

/* ---- regulators ---- */
struct regulator;
struct regulator_bulk_data { const char *supply; struct regulator *consumer; int ret; };
int devm_regulator_bulk_get(struct device *dev, int num, struct regulator_bulk_data *consumers);
int regulator_bulk_enable(int num, struct regulator_bulk_data *consumers);
int regulator_bulk_disable(int num, struct regulator_bulk_data *consumers);

/* ---- runtime PM (counted; the callbacks run synchronously: shim/rt) ---- */
int pm_runtime_resume_and_get(struct device *dev);
int pm_runtime_get_sync(struct device *dev);
int pm_runtime_get_if_in_use(struct device *dev);
int pm_runtime_put(struct device *dev);
int pm_runtime_put_sync(struct device *dev);
int pm_runtime_put_autosuspend(struct device *dev);
int pm_runtime_put_noidle(struct device *dev);
int pm_runtime_idle(struct device *dev);
void pm_runtime_enable(struct device *dev);
void pm_runtime_disable(struct device *dev);
int pm_runtime_set_active(struct device *dev);
void pm_runtime_set_suspended(struct device *dev);
bool pm_runtime_status_suspended(struct device *dev);
void pm_runtime_use_autosuspend(struct device *dev);
void pm_runtime_dont_use_autosuspend(struct device *dev);
void pm_runtime_set_autosuspend_delay(struct device *dev, int delay);
void pm_runtime_get_noresume(struct device *dev);
void pm_runtime_mark_last_busy(struct device *dev);
int pm_runtime_force_suspend(struct device *dev);
int pm_runtime_force_resume(struct device *dev);
#define SET_RUNTIME_PM_OPS(s, r, i) .runtime_suspend = s, .runtime_resume = r, .runtime_idle = i,
#define SET_SYSTEM_SLEEP_PM_OPS(s, r) .suspend = s, .resume = r,
#define SYSTEM_SLEEP_PM_OPS(s, r) .suspend = s, .resume = r,
#define RUNTIME_PM_OPS(s, r, i) .runtime_suspend = s, .runtime_resume = r, .runtime_idle = i,
#define DEFINE_RUNTIME_DEV_PM_OPS(name, s, r, i) \
	const struct dev_pm_ops name = { .runtime_suspend = s, .runtime_resume = r, .runtime_idle = i }
#define pm_ptr(p) (p)
#define pm_sleep_ptr(p) (p)

/* ---- DMA (coherent buffers = uncached, physically contiguous kernel memory: glue) ---- */
enum dma_data_direction { DMA_BIDIRECTIONAL = 0, DMA_TO_DEVICE = 1, DMA_FROM_DEVICE = 2, DMA_NONE = 3 };
void *dma_alloc_coherent(struct device *dev, size_t size, dma_addr_t *dma_handle, gfp_t gfp);
void dma_free_coherent(struct device *dev, size_t size, void *cpu_addr, dma_addr_t dma_handle);
#ifndef DMA_BIT_MASK
#define DMA_BIT_MASK(n) (((n) == 64) ? ~0ULL : ((1ULL << (n)) - 1))
#endif
struct sg_table;
struct dma_buf;
struct dma_buf_attachment;
struct iosys_map;

/* ---- regmap (MMIO, 32-bit registers: sun6i-csi) ---- */
struct regmap;
struct regmap_config {
	const char *name;
	int reg_bits;
	int reg_stride;
	int val_bits;
	unsigned int max_register;
	bool fast_io;
};
struct regmap *devm_regmap_init_mmio_clk(struct device *dev, const char *clk_id, void __iomem *regs,
	const struct regmap_config *config);
int regmap_write(struct regmap *map, unsigned int reg, unsigned int val);
int regmap_read(struct regmap *map, unsigned int reg, unsigned int *val);
int regmap_update_bits(struct regmap *map, unsigned int reg, unsigned int mask, unsigned int val);

/* ---- I2C ---- */
struct rt_mutex { struct mutex m; };
#define I2C_NAME_SIZE 20
#define I2C_M_RD 0x0001
#define I2C_M_TEN 0x0010
#define I2C_M_DMA_SAFE 0x0200
#define I2C_M_RECV_LEN 0x0400
#define I2C_M_NO_RD_ACK 0x0800
#define I2C_M_IGNORE_NAK 0x1000
#define I2C_M_REV_DIR_ADDR 0x2000
#define I2C_M_NOSTART 0x4000
#define I2C_M_STOP 0x8000
#define I2C_SMBUS_BLOCK_MAX 32
#define I2C_FUNC_I2C 0x00000001
#define I2C_FUNC_10BIT_ADDR 0x00000002
#define I2C_FUNC_PROTOCOL_MANGLING 0x00000004
#define I2C_FUNC_NOSTART 0x00000010
#define I2C_FUNC_SMBUS_EMUL 0x0eff0008
#define I2C_FUNC_SMBUS_READ_BLOCK_DATA 0x01000000
#define I2C_FUNC_SMBUS_EMUL_ALL (I2C_FUNC_SMBUS_EMUL | I2C_FUNC_SMBUS_READ_BLOCK_DATA)
#define I2C_CLIENT_END 0xfffeU
#define I2C_CLASS_HWMON (1 << 0)
#define I2C_CLASS_DEPRECATED (1 << 8)
#define I2C_AQ_NO_CLK_STRETCH (1 << 4)
#define I2C_LOCK_ROOT_ADAPTER 0x01
#define I2C_LOCK_SEGMENT 0x02
struct i2c_msg { u16 addr; u16 flags; u16 len; u8 *buf; };
struct i2c_adapter;
struct i2c_client;
union i2c_smbus_data;
struct i2c_algorithm {
	int (*xfer)(struct i2c_adapter *adap, struct i2c_msg *msgs, int num);
	int (*xfer_atomic)(struct i2c_adapter *adap, struct i2c_msg *msgs, int num);
	int (*smbus_xfer)(struct i2c_adapter *adap, u16 addr, unsigned short flags, char read_write, u8 command,
		int size, union i2c_smbus_data *data);
	u32 (*functionality)(struct i2c_adapter *adap);
};
struct i2c_lock_operations {
	void (*lock_bus)(struct i2c_adapter *adapter, unsigned int flags);
	int (*trylock_bus)(struct i2c_adapter *adapter, unsigned int flags);
	void (*unlock_bus)(struct i2c_adapter *adapter, unsigned int flags);
};
struct i2c_adapter_quirks { u64 flags; int max_num_msgs; u16 max_write_len, max_read_len, max_comb_1st_msg_len,
	max_comb_2nd_msg_len; };
struct i2c_adapter {
	struct module *owner;
	unsigned int class;
	const struct i2c_algorithm *algo;
	void *algo_data;
	const struct i2c_lock_operations *lock_ops;
	struct rt_mutex bus_lock;
	int timeout;
	int retries;
	struct device dev;
	int nr;
	char name[48];
	const struct i2c_adapter_quirks *quirks;
};
#define to_i2c_adapter(d) container_of(d, struct i2c_adapter, dev)
#define i2c_get_adapdata(a) dev_get_drvdata(&(a)->dev)
#define i2c_set_adapdata(a, p) dev_set_drvdata(&(a)->dev, p)
int i2c_add_adapter(struct i2c_adapter *adap);
int i2c_add_numbered_adapter(struct i2c_adapter *adap);
void i2c_del_adapter(struct i2c_adapter *adap);
int i2c_transfer(struct i2c_adapter *adap, struct i2c_msg *msgs, int num);
int __i2c_transfer(struct i2c_adapter *adap, struct i2c_msg *msgs, int num);
void i2c_lock_bus(struct i2c_adapter *adap, unsigned int flags);
void i2c_unlock_bus(struct i2c_adapter *adap, unsigned int flags);
#define i2c_adapter_depth(a) 0
static inline u8 i2c_8bit_addr_from_msg(const struct i2c_msg *msg)
{ return (u8)((msg->addr << 1) | (msg->flags & I2C_M_RD)); }
struct i2c_device_id { char name[I2C_NAME_SIZE]; unsigned long driver_data; };
struct i2c_client {
	unsigned short flags;
	unsigned short addr;
	char name[I2C_NAME_SIZE];
	struct i2c_adapter *adapter;
	struct device dev;
	int irq;
};
struct i2c_driver {
	int (*probe)(struct i2c_client *client);
	void (*remove)(struct i2c_client *client);
	struct device_driver driver;
	const struct i2c_device_id *id_table;
};
#define to_i2c_client(d) container_of(d, struct i2c_client, dev)
#define i2c_get_clientdata(c) dev_get_drvdata(&(c)->dev)
#define i2c_set_clientdata(c, p) dev_set_drvdata(&(c)->dev, p)
struct i2c_client *i2c_verify_client(struct device *dev);
const struct i2c_device_id *i2c_client_get_device_id(const struct i2c_client *client);

/* SPI: only named in v4l2-common prototypes */
struct spi_device;
struct spi_board_info;
struct spi_controller;

/* ---- firmware files (user-supplied: never shipped; the Symbian side reads them) ---- */
#ifndef PHE_HAVE_FIRMWARE
struct firmware { size_t size; const u8 *data; void *priv; };
#endif
int phe_request_firmware(const struct firmware **fw, const char *name, struct device *device);
#undef request_firmware
#undef release_firmware
#define request_firmware(fw, name, dev) phe_request_firmware(fw, name, dev)
#define firmware_request_nowarn(fw, name, dev) phe_request_firmware(fw, name, dev)
void phe_release_firmware(const struct firmware *fw);
#define release_firmware(fw) phe_release_firmware(fw)

/* ---- debugfs + seq_file (debugfs is off: every create returns an error pointer the drivers ignore) ---- */
struct dentry;
struct seq_file { void *private; };
#define debugfs_create_dir(n, p) ((struct dentry *)ERR_PTR(-ENODEV))
#define debugfs_create_file(n, m, p, d, f) ((struct dentry *)ERR_PTR(-ENODEV))
#define debugfs_create_file_unsafe(n, m, p, d, f) ((struct dentry *)ERR_PTR(-ENODEV))
#define debugfs_create_u8(n, m, p, v) ((void)(v))
#define debugfs_create_u32(n, m, p, v) ((void)(v))
#define debugfs_create_bool(n, m, p, v) ((void)(v))
#define debugfs_remove_recursive(d) ((void)(d))
#define debugfs_remove(d) ((void)(d))
int seq_printf(struct seq_file *m, const char *fmt, ...) __printf(2, 3);
int seq_puts(struct seq_file *m, const char *s);

/* ---- ctype ---- */
#define isspace(c) ((c) == ' ' || ((c) >= '\t' && (c) <= '\r'))
#define isdigit(c) ((c) >= '0' && (c) <= '9')
#define isprint(c) ((c) >= 0x20 && (c) < 0x7f)
#define tolower(c) (((c) >= 'A' && (c) <= 'Z') ? (c) + 32 : (c))

/* ---- ioctl encoding (the uapi media headers use _IOWR etc.) ---- */
#define _IOC_NRBITS 8
#define _IOC_TYPEBITS 8
#define _IOC_SIZEBITS 14
#define _IOC_NRSHIFT 0
#define _IOC_TYPESHIFT (_IOC_NRSHIFT + _IOC_NRBITS)
#define _IOC_SIZESHIFT (_IOC_TYPESHIFT + _IOC_TYPEBITS)
#define _IOC_DIRSHIFT (_IOC_SIZESHIFT + _IOC_SIZEBITS)
#define _IOC_NONE 0U
#define _IOC_WRITE 1U
#define _IOC_READ 2U
#define _IOC(dir, type, nr, size) (((dir) << _IOC_DIRSHIFT) | ((type) << _IOC_TYPESHIFT) | ((nr) << _IOC_NRSHIFT) | \
	((size) << _IOC_SIZESHIFT))
#define _IO(t, n) _IOC(_IOC_NONE, (t), (n), 0)
#define _IOR(t, n, s) _IOC(_IOC_READ, (t), (n), sizeof(s))
#define _IOW(t, n, s) _IOC(_IOC_WRITE, (t), (n), sizeof(s))
#define _IOWR(t, n, s) _IOC(_IOC_READ | _IOC_WRITE, (t), (n), sizeof(s))
#define _IOC_NR(nr) (((nr) >> _IOC_NRSHIFT) & ((1 << _IOC_NRBITS) - 1))
#define _IOC_TYPE(nr) (((nr) >> _IOC_TYPESHIFT) & ((1 << _IOC_TYPEBITS) - 1))
#define _IOC_SIZE(nr) (((nr) >> _IOC_SIZESHIFT) & ((1 << _IOC_SIZEBITS) - 1))
#define _IOC_DIR(nr) (((nr) >> _IOC_DIRSHIFT) & 3)

#endif
