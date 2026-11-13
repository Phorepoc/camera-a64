/* Copyright The Phorepoc Authors
 * SPDX-License-Identifier: GPL-2.0-only */

/* phe_misc.h -- the rest of the shim (included last by phe_linux.h): limits, sizes, rt_mutex, cleanup guards, file
 * operations, sscanf. Same rules as phe_dev.h. */
#ifndef PHE_MISC_H
#define PHE_MISC_H

/* ---- more kernel.h / limits / sizes ---- */
#ifndef ULONG_MAX
#define ULONG_MAX (~0UL)
#endif
#ifndef LONG_MAX
#define LONG_MAX ((long)(~0UL >> 1))
#endif
#ifndef U64_MAX
#define U64_MAX (~0ULL)
#endif
#ifndef SZ_1M
#define SZ_1M 0x00100000
#endif
#define SZ_2M 0x00200000
#define SZ_4M 0x00400000
#define SZ_16M 0x01000000
#define SZ_32M 0x02000000
#define SZ_256 0x100
#define SZ_512 0x200
#define round_down(x, y) ((x) & ~((__typeof__(x))((y) - 1)))
#define round_up(x, y) ((((x) - 1) | ((__typeof__(x))((y) - 1))) + 1)
#define container_of_const(ptr, type, member) container_of(ptr, type, member)
#define ERR_CAST(p) ((void *)(p))
int sscanf(const char *buf, const char *fmt, ...);

/* ---- locks: rt_mutex (the I2C bus lock), interruptible locking, lockdep classes, cleanup guards ---- */
#define rt_mutex_init(l) mutex_init(&(l)->m)
#define rt_mutex_lock(l) mutex_lock(&(l)->m)
#define rt_mutex_lock_nested(l, s) mutex_lock(&(l)->m)
#define rt_mutex_trylock(l) mutex_trylock(&(l)->m)
#define rt_mutex_unlock(l) mutex_unlock(&(l)->m)
#define mutex_lock_interruptible(m) (mutex_lock(m), 0)
#define mutex_lock_nested(m, s) mutex_lock(m)
struct lock_class_key { int unused; };
#define lockdep_set_class(l, k) ((void)(k))
#define lockdep_register_key(k) ((void)(k))
#define lockdep_unregister_key(k) ((void)(k))
static inline bool refcount_dec_and_mutex_lock(refcount_t *r, struct mutex *lock)
{ if (--r->refs.counter) return false; mutex_lock(lock); return true; }
/* cleanup.h: guard(mutex)(&m) and scoped_guard(mutex, &m) { ... } */
static inline void __phe_guard_mutex_exit(struct mutex **m) { if (*m) mutex_unlock(*m); }
static inline struct mutex *__phe_guard_mutex_enter(struct mutex *m) { mutex_lock(m); return m; }
#define __PHE_UNIQ(p) __CONCAT(p, __COUNTER__)
#define guard(kind) __phe_guard_##kind(__PHE_UNIQ(__phe_g_))
#define __phe_guard_mutex(name) struct mutex *name __attribute__((cleanup(__phe_guard_mutex_exit))) = __phe_guard_mutex_enter
#define scoped_guard(kind, arg) \
	for (struct mutex *__phe_sg __attribute__((cleanup(__phe_guard_mutex_exit))) = __phe_guard_mutex_enter(arg), \
	     *__phe_once = (struct mutex *)1; __phe_once; __phe_once = NULL)

/* ---- file operations (debugfs attribute files: compiled, never registered) ---- */
struct file_operations {
	struct module *owner;
	loff_t (*llseek)(struct file *, loff_t, int);
	ssize_t (*read)(struct file *, char __user *, size_t, loff_t *);
	ssize_t (*write)(struct file *, const char __user *, size_t, loff_t *);
	__poll_t (*poll)(struct file *, struct poll_table_struct *);
	long (*unlocked_ioctl)(struct file *, unsigned int, unsigned long);
	long (*compat_ioctl)(struct file *, unsigned int, unsigned long);
	int (*mmap)(struct file *, struct vm_area_struct *);
	int (*open)(struct inode *, struct file *);
	int (*release)(struct inode *, struct file *);
	unsigned long (*get_unmapped_area)(struct file *, unsigned long, unsigned long, unsigned long, unsigned long);
};
int simple_open(struct inode *inode, struct file *file);
loff_t noop_llseek(struct file *file, loff_t offset, int whence);
loff_t default_llseek(struct file *file, loff_t offset, int whence);
ssize_t simple_read_from_buffer(void __user *to, size_t count, loff_t *ppos, const void *from, size_t available);
ssize_t simple_write_to_buffer(void *to, size_t available, loff_t *ppos, const void __user *from, size_t count);
#define copy_from_user(d, s, n) (memcpy(d, s, n), 0UL)
#define copy_to_user(d, s, n) (memcpy(d, s, n), 0UL)
#define DEFINE_SHOW_ATTRIBUTE(__name) \
	static const struct file_operations __name##_fops __attribute__((unused)) = { .owner = THIS_MODULE }
int desc_to_gpio(const struct gpio_desc *desc);
#define DEFINE_DEBUGFS_ATTRIBUTE(__fops, __get, __set, __fmt) \
	static const struct file_operations __fops __attribute__((unused)) = { .owner = THIS_MODULE }

#endif
