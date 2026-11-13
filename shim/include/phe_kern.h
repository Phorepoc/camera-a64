/* Copyright The Phorepoc Authors
 * SPDX-License-Identifier: GPL-2.0-only */

/* phe_kern.h -- what the vendored media core (v4l2-core, videobuf2, mc) needs beyond the drivers (included by
 * phe_linux.h after phe_misc.h): char-device numbers, sysfs attributes, modules, pages and mmap, scatterlists,
 * dma-buf, file descriptors, wait queues, sleeping, string helpers. Same rules as phe_dev.h. Paths our glue does not
 * use (mmap of buffers into a Linux process, dma-buf export/import, char-device nodes) compile against declarations
 * whose runtime returns an error. */
#ifndef PHE_KERN_H
#define PHE_KERN_H

/* ---- versions, namespaces, modules ---- */
#define KERNEL_VERSION(a, b, c) (((a) << 16) + ((b) << 8) + ((c) > 255 ? 255 : (c)))
#define LINUX_VERSION_CODE KERNEL_VERSION(7, 3, 0)
#define MODULE_IMPORT_NS(ns)
#define MODULE_ALIAS_CHARDEV_MAJOR(m)
#define MODULE_ALIAS_CHARDEV(ma, mi)
#define EXPORT_SYMBOL_NS_GPL(s, ns)
#define EXPORT_SYMBOL_NS(s, ns)
#define try_module_get(m) true
#define module_put(m) ((void)(m))
#define request_module(...) (-ENOENT)
#define request_module_nowait(...) (-ENOENT)
#define I2C_MODULE_PREFIX "i2c:"
#define pr_warn_once pr_warn
#define pr_info_once pr_info
#define pr_err_once pr_err
#ifndef ENOTUNIQ
#define ENOTUNIQ 76
#endif
#ifndef ENOTBLK
#define ENOTBLK 15
#endif

/* ---- char devices ---- */
typedef u32 dev_t;
#define MINORBITS 20
#define MINORMASK ((1U << MINORBITS) - 1)
#define MAJOR(dev) ((unsigned int)((dev) >> MINORBITS))
#define MINOR(dev) ((unsigned int)((dev) & MINORMASK))
#define MKDEV(ma, mi) (((ma) << MINORBITS) | (mi))
int register_chrdev_region(dev_t from, unsigned count, const char *name);
int alloc_chrdev_region(dev_t *dev, unsigned baseminor, unsigned count, const char *name);
void unregister_chrdev_region(dev_t from, unsigned count);
void cdev_init(struct cdev *cdev, const struct file_operations *fops);
int cdev_add(struct cdev *p, dev_t dev, unsigned count);
int cdev_device_add(struct cdev *cdev, struct device *dev);
void cdev_device_del(struct cdev *cdev, struct device *dev);
void cdev_del(struct cdev *p);
#define iminor(inode) MINOR((inode)->i_rdev)
#define nonseekable_open(i, f) 0
#define no_llseek NULL
#define O_CLOEXEC 02000000
#define O_ACCMODE 00000003
#define O_RDWR 00000002

/* ---- sysfs attributes and classes ---- */
struct attribute { const char *name; umode_t mode; };
struct attribute_group { const char *name; struct attribute **attrs; };
struct device_attribute {
	struct attribute attr;
	ssize_t (*show)(struct device *dev, struct device_attribute *attr, char *buf);
	ssize_t (*store)(struct device *dev, struct device_attribute *attr, const char *buf, size_t count);
};
#define __ATTR(_name, _mode, _show, _store) { .attr = { .name = #_name, .mode = _mode }, .show = _show, .store = _store }
#define DEVICE_ATTR_RO(_name) struct device_attribute dev_attr_##_name = __ATTR(_name, 0444, _name##_show, NULL)
#define DEVICE_ATTR_RW(_name) struct device_attribute dev_attr_##_name = __ATTR(_name, 0644, _name##_show, _name##_store)
#define DEVICE_ATTR(_name, _mode, _show, _store) struct device_attribute dev_attr_##_name = __ATTR(_name, _mode, _show, _store)
#define ATTRIBUTE_GROUPS(_name) \
	static const struct attribute_group _name##_group = { .attrs = _name##_attrs }; \
	static const struct attribute_group *_name##_groups[] __attribute__((unused)) = { &_name##_group, NULL }
struct class { const char *name; const struct attribute_group **dev_groups; char *(*devnode)(const struct device *dev, umode_t *mode); };
int class_register(struct class *cls);
void class_unregister(struct class *cls);
int device_create_file(struct device *dev, const struct device_attribute *attr);
void device_remove_file(struct device *dev, const struct device_attribute *attr);
int device_register(struct device *dev);
void device_unregister(struct device *dev);
void device_initialize(struct device *dev);
int device_add(struct device *dev);
void device_del(struct device *dev);
int dev_set_name(struct device *dev, const char *name, ...) __printf(2, 3);
#define sysfs_emit(buf, ...) shim_snprintf(buf, 4096, __VA_ARGS__)
#define kobject_uevent(k, a) 0

/* ---- memory: pages, kvmalloc, kasprintf, bitmaps ---- */
#define PAGE_SHIFT 12
#define PAGE_SIZE (1UL << PAGE_SHIFT)
#define PAGE_MASK (~(PAGE_SIZE - 1))
#define PAGE_ALIGN(x) ALIGN(x, PAGE_SIZE)
#define offset_in_page(p) ((unsigned long)(p) & ~PAGE_MASK)
#define kvmalloc(n, g) shim_alloc(n)
#define kvzalloc(n, g) shim_zalloc(n)
#define kvcalloc(n, sz, g) shim_zalloc((size_t)(n) * (size_t)(sz))
#define kvmalloc_array(n, sz, g) shim_alloc((size_t)(n) * (size_t)(sz))
#define kvzalloc_objs(obj, n, ...) shim_zalloc(sizeof(obj) * (size_t)(n))
#define kzalloc_objs(obj, n, ...) shim_zalloc(sizeof(obj) * (size_t)(n))
#define kvzalloc_flex(obj, member, n, ...) shim_zalloc(sizeof(obj) + sizeof((obj).member[0]) * (size_t)(n))
#define kzalloc_flex(obj, member, n, ...) shim_zalloc(sizeof(obj) + sizeof((obj).member[0]) * (size_t)(n))
#define krealloc(p, n, g) phe_krealloc(p, n)
void *phe_krealloc(void *p, size_t n);
char *kasprintf(gfp_t gfp, const char *fmt, ...) __printf(2, 3);
char *kvasprintf(gfp_t gfp, const char *fmt, va_list ap);
static inline void __free_kfree(void *p) { shim_free(*(void **)p); }
#define bitmap_zalloc(n, g) ((unsigned long *)shim_zalloc(BITS_TO_LONGS(n) * sizeof(long)))
#define bitmap_free(b) shim_free(b)
#define bitmap_clear(b, s, n) do { unsigned int __i; for (__i = (s); __i < (s) + (n); ++__i) clear_bit(__i, b); } while (0)
#define bitmap_set(b, s, n) do { unsigned int __i; for (__i = (s); __i < (s) + (n); ++__i) set_bit(__i, b); } while (0)
static inline void bitmap_or(unsigned long *d, const unsigned long *a, const unsigned long *b, unsigned int bits)
{ unsigned int i; for (i = 0; i < BITS_TO_LONGS(bits); ++i) d[i] = a[i] | b[i]; }
static inline void bitmap_copy(unsigned long *d, const unsigned long *s, unsigned int bits)
{ memcpy(d, s, BITS_TO_LONGS(bits) * sizeof(long)); }
#define memset_after(obj, v, member) \
	memset((u8 *)(obj) + offsetofend(__typeof__(*(obj)), member), v, \
	       sizeof(*(obj)) - offsetofend(__typeof__(*(obj)), member))
#define memset_startat(obj, v, member) \
	memset((u8 *)(obj) + offsetof(__typeof__(*(obj)), member), v, sizeof(*(obj)) - offsetof(__typeof__(*(obj)), member))
#ifndef sizeof_field
#define sizeof_field(t, m) sizeof(((t *)0)->m)
#endif
#define check_mul_overflow(a, b, d) __builtin_mul_overflow(a, b, d)
#define check_add_overflow(a, b, d) __builtin_add_overflow(a, b, d)
#define size_add(a, b) ((size_t)(a) + (size_t)(b))
#define flex_array_size(p, m, n) (sizeof((p)->m[0]) * (size_t)(n))
#define array_index_nospec(i, n) (i)
#define __phe_first(a, ...) a
#define strscpy(d, s, ...) phe_strscpy(d, s, __phe_first(__VA_ARGS__ __VA_OPT__(,) sizeof(d)))
ssize_t phe_strscpy(char *dst, const char *src, size_t size);
#define strscpy_pad strscpy
size_t strlcpy(char *dst, const char *src, size_t size);
char *strchr(const char *s, int c);
char *strrchr(const char *s, int c);
size_t strnlen(const char *s, size_t n);
int strcasecmp(const char *a, const char *b);
#define kstrtouint(s, b, r) phe_kstrtou32(s, b, r)
int phe_kstrtou32(const char *s, unsigned int base, u32 *res);
#define kstrtobool(s, r) phe_kstrtobool(s, r)
int phe_kstrtobool(const char *s, bool *res);

/* ---- user pages, mmap (never used: buffers live in Symbian chunks) ---- */
struct mm_struct;
struct vm_operations_struct {
	void (*open)(struct vm_area_struct *area);
	void (*close)(struct vm_area_struct *area);
};
struct vm_area_struct {
	unsigned long vm_start, vm_end, vm_pgoff, vm_flags;
	void *vm_private_data;
	const struct vm_operations_struct *vm_ops;
	struct file *vm_file;
	struct mm_struct *vm_mm;
};
#define VM_READ 0x1
#define VM_WRITE 0x2
#define VM_SHARED 0x8
#define VM_DONTEXPAND 0x00040000
#define VM_DONTDUMP 0x04000000
#define VM_IO 0x00004000
#define vm_flags_set(v, f) ((v)->vm_flags |= (f))
#define vm_flags_clear(v, f) ((v)->vm_flags &= ~(f))
#define FOLL_WRITE 0x01
#define FOLL_FORCE 0x10
#define FOLL_LONGTERM 0x100
int pin_user_pages_fast(unsigned long start, int nr_pages, unsigned int gup_flags, struct page **pages);
void unpin_user_pages(struct page **pages, unsigned long npages);
void unpin_user_pages_dirty_lock(struct page **pages, unsigned long npages, bool make_dirty);
struct vm_area_struct *vma_lookup(struct mm_struct *mm, unsigned long addr);
int remap_pfn_range(struct vm_area_struct *vma, unsigned long addr, unsigned long pfn, unsigned long size, int prot);
#define page_to_pfn(p) phe_page_to_pfn(p)
unsigned long phe_page_to_pfn(struct page *p);
#define pfn_to_page(n) phe_pfn_to_page(n)
struct page *phe_pfn_to_page(unsigned long pfn);
#define page_address(p) phe_page_address(p)
void *phe_page_address(struct page *p);
#define virt_to_page(v) phe_pfn_to_page(0)
#define get_page(p) ((void)(p))
#define put_page(p) ((void)(p))
#define set_page_dirty_lock(p) ((void)(p))
#define is_vmalloc_addr(p) false
#define vm_map_ram(pages, n, node) NULL
#define vm_unmap_ram(p, n) ((void)0)
#define current_mm_phe NULL
#define mmap_read_lock(mm) ((void)0)
#define mmap_read_unlock(mm) ((void)0)
#define PAGE_KERNEL 0
#define pgprot_noncached(p) (p)
#define pgprot_writecombine(p) (p)

/* ---- scatterlists ---- */
struct scatterlist { unsigned long page_link; unsigned int offset, length; dma_addr_t dma_address; unsigned int dma_length; };
struct sg_table { struct scatterlist *sgl; unsigned int nents, orig_nents; };
#define sg_dma_address(sg) ((sg)->dma_address)
#define sg_dma_len(sg) ((sg)->dma_length)
#define sg_next(sg) ((sg) + 1)
#define sg_page(sg) ((struct page *)((sg)->page_link))
#define sg_set_page(sg, p, len, off) ((sg)->page_link = (unsigned long)(p), (sg)->length = (len), (sg)->offset = (off))
#define for_each_sg(sglist, sg, nr, i) for ((i) = 0, (sg) = (sglist); (i) < (nr); (i)++, (sg) = sg_next(sg))
#define for_each_sgtable_sg(sgt, sg, i) for_each_sg((sgt)->sgl, sg, (sgt)->orig_nents, i)
#define for_each_sgtable_dma_sg(sgt, sg, i) for_each_sg((sgt)->sgl, sg, (sgt)->nents, i)
int sg_alloc_table(struct sg_table *t, unsigned int nents, gfp_t gfp);
void sg_free_table(struct sg_table *t);
int sg_alloc_table_from_pages(struct sg_table *sgt, struct page **pages, unsigned int n_pages, unsigned int offset,
	unsigned long size, gfp_t gfp);
int dma_get_sgtable_attrs(struct device *dev, struct sg_table *sgt, void *cpu_addr, dma_addr_t dma_addr,
	size_t size, unsigned long attrs);
int dma_map_sgtable(struct device *dev, struct sg_table *sgt, enum dma_data_direction dir, unsigned long attrs);
void dma_unmap_sgtable(struct device *dev, struct sg_table *sgt, enum dma_data_direction dir, unsigned long attrs);
void dma_sync_sgtable_for_cpu(struct device *dev, struct sg_table *sgt, enum dma_data_direction dir);
void dma_sync_sgtable_for_device(struct device *dev, struct sg_table *sgt, enum dma_data_direction dir);

/* ---- DMA attributes and non-contiguous paths ---- */
#define DMA_ATTR_WRITE_COMBINE (1UL << 2)
#define DMA_ATTR_NO_KERNEL_MAPPING (1UL << 4)
#define DMA_ATTR_SKIP_CPU_SYNC (1UL << 5)
#define DMA_ATTR_ALLOC_SINGLE_PAGES (1UL << 7)
#define DMA_ATTR_NO_WARN (1UL << 8)
#define DMA_MAPPING_ERROR (~(dma_addr_t)0)
void *dma_alloc_attrs(struct device *dev, size_t size, dma_addr_t *dma_handle, gfp_t flag, unsigned long attrs);
void dma_free_attrs(struct device *dev, size_t size, void *cpu_addr, dma_addr_t dma_handle, unsigned long attrs);
int dma_mmap_attrs(struct device *dev, struct vm_area_struct *vma, void *cpu_addr, dma_addr_t dma_addr, size_t size,
	unsigned long attrs);
struct sg_table *dma_alloc_noncontiguous(struct device *dev, size_t size, enum dma_data_direction dir, gfp_t gfp,
	unsigned long attrs);
void dma_free_noncontiguous(struct device *dev, size_t size, struct sg_table *sgt, enum dma_data_direction dir);
void *dma_vmap_noncontiguous(struct device *dev, size_t size, struct sg_table *sgt);
void dma_vunmap_noncontiguous(struct device *dev, void *vaddr);
int dma_mmap_noncontiguous(struct device *dev, struct vm_area_struct *vma, size_t size, struct sg_table *sgt);
dma_addr_t dma_map_resource(struct device *dev, phys_addr_t phys_addr, size_t size, enum dma_data_direction dir,
	unsigned long attrs);
void dma_unmap_resource(struct device *dev, dma_addr_t addr, size_t size, enum dma_data_direction dir, unsigned long attrs);
int dma_mapping_error(struct device *dev, dma_addr_t dma_addr);
unsigned long dma_get_merge_boundary(struct device *dev);
#define dma_get_cache_alignment() 64
#define flush_kernel_vmap_range(p, n) ((void)0)
#define invalidate_kernel_vmap_range(p, n) ((void)0)

/* ---- dma-buf (export/import of buffers to other Linux drivers: unused here) ---- */
struct iosys_map { void *vaddr; bool is_iomem; };
#define IOSYS_MAP_INIT_VADDR(v) { .vaddr = (v), .is_iomem = false }
#define iosys_map_set_vaddr(m, v) ((m)->vaddr = (v), (m)->is_iomem = false)
#define iosys_map_clear(m) ((m)->vaddr = NULL)
struct dma_buf_attachment {
	struct dma_buf *dmabuf;
	struct device *dev;
	void *priv;
};
struct dma_buf_ops {
	int (*attach)(struct dma_buf *, struct dma_buf_attachment *);
	void (*detach)(struct dma_buf *, struct dma_buf_attachment *);
	struct sg_table *(*map_dma_buf)(struct dma_buf_attachment *, enum dma_data_direction);
	void (*unmap_dma_buf)(struct dma_buf_attachment *, struct sg_table *, enum dma_data_direction);
	void (*release)(struct dma_buf *);
	int (*begin_cpu_access)(struct dma_buf *, enum dma_data_direction);
	int (*end_cpu_access)(struct dma_buf *, enum dma_data_direction);
	int (*mmap)(struct dma_buf *, struct vm_area_struct *vma);
	int (*vmap)(struct dma_buf *dmabuf, struct iosys_map *map);
	void (*vunmap)(struct dma_buf *dmabuf, struct iosys_map *map);
};
struct dma_buf { size_t size; void *priv; const struct dma_buf_ops *ops; struct file *file; };
struct dma_buf_export_info {
	const char *exp_name; struct module *owner; const struct dma_buf_ops *ops; size_t size; int flags; void *priv;
};
#define DEFINE_DMA_BUF_EXPORT_INFO(name) struct dma_buf_export_info name = { .exp_name = KBUILD_MODNAME }
struct dma_buf *dma_buf_export(const struct dma_buf_export_info *exp_info);
struct dma_buf *dma_buf_get(int fd);
void dma_buf_put(struct dma_buf *dmabuf);
int dma_buf_fd(struct dma_buf *dmabuf, int flags);
struct dma_buf_attachment *dma_buf_attach(struct dma_buf *dmabuf, struct device *dev);
void dma_buf_detach(struct dma_buf *dmabuf, struct dma_buf_attachment *attach);
struct sg_table *dma_buf_map_attachment_unlocked(struct dma_buf_attachment *, enum dma_data_direction);
void dma_buf_unmap_attachment_unlocked(struct dma_buf_attachment *, struct sg_table *, enum dma_data_direction);
int dma_buf_vmap_unlocked(struct dma_buf *dmabuf, struct iosys_map *map);
void dma_buf_vunmap_unlocked(struct dma_buf *dmabuf, struct iosys_map *map);
#define get_dma_buf(d) ((void)(d))

/* ---- file descriptors (media requests: unused here) ---- */
struct fd { struct file *file; };
#define fd_file(f) ((f).file)
#define fd_empty(f) (!(f).file)
struct fd phe_fdget(int fd);
void phe_fdput(struct fd f);
static inline void __phe_fd_cleanup(struct fd *f) { phe_fdput(*f); }
#define CLASS(kind, var) struct fd var __attribute__((cleanup(__phe_fd_cleanup))) = phe_fdget
struct file *anon_inode_getfile(const char *name, const struct file_operations *fops, void *priv, int flags);
int get_unused_fd_flags(unsigned flags);
void put_unused_fd(unsigned int fd);
void fd_install(unsigned int fd, struct file *file);
void fput(struct file *file);
#define FD_PREPARE(name, flags, expr) \
	struct file *name##_file = (expr); int name##_fd = IS_ERR(name##_file) ? (int)PTR_ERR(name##_file) : get_unused_fd_flags(flags)
#define DEFAULT_POLLMASK (EPOLLIN | EPOLLOUT | EPOLLRDNORM | EPOLLWRNORM)
__poll_t poll_requested_events(const poll_table *p);

/* ---- sleeping and waiting ---- */
#define TASK_RUNNING 0
#define TASK_INTERRUPTIBLE 1
#define TASK_UNINTERRUPTIBLE 2
#define set_current_state(s) ((void)(s))
#define __set_current_state(s) ((void)(s))
#define schedule() shim_msleep(1)
#define schedule_timeout(t) (shim_msleep(jiffies_to_msecs(t)), 0)
#define wait_event(q, c) do { while (!(c)) shim_msleep(1); } while (0)
#define wait_event_interruptible(q, c) ({ while (!(c)) shim_msleep(1); 0; })
#define wait_event_timeout(q, c, t) ({ unsigned long __n = jiffies_to_msecs(t); while (!(c) && __n) { shim_msleep(1); __n--; } (c) ? 1 : 0; })
#define wake_up_interruptible_all(q) wake_up(q)
#define wake_up_interruptible_poll(q, m) wake_up(q)
#define wake_up_poll(q, m) wake_up(q)
#define freezable_schedule() schedule()
#define set_freezable() ((void)0)
#define try_to_freeze() 0

/* ---- lists (the iterators phe_core lacks) ---- */
#ifndef list_is_singular
#define list_is_singular(h) (!list_empty(h) && (h)->next == (h)->prev)
#endif
#define list_for_each_entry_continue(pos, head, member) \
	for (pos = list_next_entry(pos, member); &pos->member != (head); pos = list_next_entry(pos, member))
#define list_for_each_entry_continue_reverse(pos, head, member) \
	for (pos = list_prev_entry(pos, member); &pos->member != (head); pos = list_prev_entry(pos, member))
#define list_for_each_entry_from(pos, head, member) \
	for (; &pos->member != (head); pos = list_next_entry(pos, member))

/* ---- firmware-node children ---- */
#define fwnode_for_each_child_node(fwnode, child) \
	for (child = fwnode_get_next_child_node(fwnode, NULL); child; child = fwnode_get_next_child_node(fwnode, child))
#define fwnode_for_each_available_child_node fwnode_for_each_child_node
#define device_for_each_child_node(dev, child) fwnode_for_each_child_node(dev_fwnode(dev), child)

/* ---- I2C device creation (v4l2-i2c) ---- */
struct i2c_board_info { char type[I2C_NAME_SIZE]; unsigned short flags; unsigned short addr; void *platform_data;
	struct fwnode_handle *fwnode; int irq; };
#define I2C_BOARD_INFO(dev_type, dev_addr) .type = dev_type, .addr = (dev_addr)
struct i2c_client *i2c_new_client_device(struct i2c_adapter *adap, struct i2c_board_info const *info);
struct i2c_client *i2c_new_scanned_device(struct i2c_adapter *adap, struct i2c_board_info *info,
	unsigned short const *addr_list, int (*probe)(struct i2c_adapter *adap, unsigned short addr));
void i2c_unregister_device(struct i2c_client *client);
#define i2c_client_has_driver(c) ((c) && (c)->dev.driver)
#define i2c_adapter_id(a) ((a)->nr)

/* ---- clock providers (v4l2-common's sensor clock helper) ---- */
struct clk_ops;
struct clk_init_data { const char *name; const struct clk_ops *ops; const char * const *parent_names; u8 num_parents;
	unsigned long flags; };
struct clk_hw { struct clk *clk; const struct clk_init_data *init; };
struct clk *devm_clk_register_fixed_rate_phe(struct device *dev, const char *name, unsigned long rate);
struct clk_hw *devm_clk_hw_register_fixed_rate(struct device *dev, const char *name, const char *parent,
	unsigned long flags, unsigned long rate);
struct clk *devm_clk_hw_get_clk(struct device *dev, struct clk_hw *hw, const char *con_id);
int devm_of_clk_add_hw_provider(struct device *dev, struct clk_hw *(*get)(void *spec, void *data), void *data);
#define of_clk_hw_simple_get NULL


/* ---- round 2 additions ---- */
#define struct_size(p, member, n) (sizeof(*(p)) + sizeof((p)->member[0]) * (size_t)(n))
#define __free(f) __attribute__((cleanup(__free_##f)))
#define no_free_ptr(p) ({ __typeof__(p) __p = (p); (p) = NULL; __p; })
#define return_ptr(p) return no_free_ptr(p)
#define lower_32_bits(n) ((u32)((n) & 0xffffffff))
#define upper_32_bits(n) ((u32)(((u64)(n)) >> 32))
#define untagged_addr(a) (a)
#define u64_to_user_ptr(x) ((void __user *)(uintptr_t)(x))
#define in_compat_syscall() false
#define list_prev_entry(pos, member) list_entry((pos)->member.prev, __typeof__(*(pos)), member)
#define kvmalloc_objs(obj, n, ...) shim_alloc(sizeof(obj) * (size_t)(n))
#define kmalloc_objs(obj, n, ...) shim_alloc(sizeof(obj) * (size_t)(n))
#define kstrdup_const(s, g) kstrdup(s, g)
#define kfree_const(p) kfree(p)
int phe_kstrtou16(const char *s, unsigned int base, u16 *res);
#define kstrtou16(s, b, r) phe_kstrtou16(s, b, r)
#define is_acpi_node(f) false
#define assert_spin_locked(l) ((void)0)
#define lockdep_set_class_and_name(l, k, n) ((void)0)
#define __mutex_init(m, n, k) mutex_init(m)
#define VM_BUG_ON(c) ((void)0)
#define pfn_valid(p) 1
#define __pfn_to_phys(p) ((phys_addr_t)(p) << PAGE_SHIFT)
#define PFN_DOWN(x) ((x) >> PAGE_SHIFT)
#define _IOC_SIZEMASK ((1 << _IOC_SIZEBITS) - 1)
static inline unsigned long __fls(unsigned long w) { return 31 - __builtin_clzl(w); }
static inline void bitmap_andnot(unsigned long *d, const unsigned long *a, const unsigned long *b, unsigned int bits)
{ unsigned int i; for (i = 0; i < BITS_TO_LONGS(bits); ++i) d[i] = a[i] & ~b[i]; }
unsigned long bitmap_find_next_zero_area(unsigned long *map, unsigned long size, unsigned long start,
	unsigned int nr, unsigned long align_mask);
bool fwnode_graph_is_endpoint(const struct fwnode_handle *fwnode);
int device_match_fwnode(struct device *dev, const void *fwnode);
int bus_register(const struct bus_type *bus);
void bus_unregister(const struct bus_type *bus);
int kobject_set_name(void *kobj, const char *fmt, ...);
struct cdev *cdev_alloc(void);
struct inode { dev_t i_rdev; struct cdev *i_cdev; };
#define file_inode(f) ((f)->f_inode)
unsigned int dma_get_max_seg_size(struct device *dev);
int dma_set_max_seg_size(struct device *dev, unsigned int size);
/* kthreads (vb2's file-I/O emulation thread: unused here) */
struct task_struct;
struct task_struct *phe_kthread_run(int (*fn)(void *), void *data, const char *name);
#define kthread_run(fn, data, ...) phe_kthread_run(fn, data, "phe")
int kthread_stop(struct task_struct *k);
bool kthread_should_stop(void);
/* FD_PREPARE (media requests: unused here) */
struct phe_fd_prepare { int fd; struct file *file; int err; };
#undef FD_PREPARE
#define FD_PREPARE(name, flags, expr) struct phe_fd_prepare name = phe_fd_prepare(flags, expr)
struct phe_fd_prepare phe_fd_prepare(unsigned int flags, struct file *file);
#define fd_prepare_fd(p) ((p).fd)
#define fd_prepare_file(p) ((p).file)
#define fd_publish(p) phe_fd_publish(&(p))
int phe_fd_publish(struct phe_fd_prepare *p);
/* trace points: off */
#define trace_vb2_qbuf(...) ((void)0)
#define trace_vb2_dqbuf(...) ((void)0)
#define trace_vb2_buf_queue(...) ((void)0)
#define trace_vb2_buf_done(...) ((void)0)
#define trace_v4l2_qbuf(...) ((void)0)
#define trace_v4l2_dqbuf(...) ((void)0)
#endif
