// Copyright The Phorepoc Authors
// SPDX-License-Identifier: GPL-2.0-only
//
// phe_user.cpp -- the user-side half of the shim, in the camera server process: the phe_rt.h runtime (allocation,
// locks, delays, time, logging) on euser, and the phe_hw.h hardware calls on the image's "CamHw.A64" channel
// (adaptation-a64 camera/a64camhw_api.h): the CSI register window and the DMA buffers are shared chunks mapped into
// this process, clocks / reset / pins / GPIOs / rails are channel controls, and the CSI interrupt is a request that a
// dedicated thread waits on, runs the Linux handler for, and re-arms. Firmware is read from \resource\firmware\.

#include <e32base.h>
#include <e32debug.h>
#include <f32file.h>
#include <hal.h>
#include "a64camhw_api.h"	// adaptation-a64: src/phorepoc-arm/a64/camera
#include "phe_hw.h"

extern "C" int shim_vsnprintf(char *buf, size_t n, const char *fmt, va_list ap);	// shim/rt/phe_lib.c
extern "C" int shim_strcmp(const char *a, const char *b);

static RA64CamHw Hw;
static RChunk CsiChunk;

TInt PheHwOpen()
	{
	TInt r = User::LoadLogicalDevice(_L("a64camhw"));		// built into the kernel: the alias resolves to CamHw.A64
	if (r != KErrNone && r != KErrAlreadyExists && r != KErrNotFound)
		return r;
	if ((r = Hw.Open()) != KErrNone)
		return r;
	return Hw.Pinmux();
	}

// ---- memory, logging ----
extern "C" void *shim_alloc(size_t n)
	{
	void *p = User::Alloc(n);
	if (!p)
		RDebug::Printf("PH: camera-a64: alloc %u failed", (TUint)n);
	return p;
	}
extern "C" void *shim_zalloc(size_t n)
	{
	void *p = User::AllocZ(n);
	if (!p)
		RDebug::Printf("PH: camera-a64: zalloc %u failed", (TUint)n);
	return p;
	}
extern "C" void shim_free(void *p) { User::Free(p); }
extern "C" void *shim_realloc(void *p, size_t n)
	{
	void *q = User::ReAlloc(p, n);
	if (!q && n)
		RDebug::Printf("PH: camera-a64: realloc %u failed", (TUint)n);
	return q;
	}
extern "C" void shim_panic(const char *what)
	{
	RDebug::Printf("PH: camera-a64: panic: %s", what);
	User::Panic(_L("camera-a64"), 0);
	}
extern "C" void shim_log(const char *fmt, ...)
	{
	char buf[256];
	va_list ap;
	va_start(ap, fmt);
	shim_vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	RDebug::Printf("PH: %s", buf);	// the phone kernel forwards only "PH:" lines
	}

// ---- locks: struct mutex / struct spinlock { void *k; } hold an RMutex / RFastLock on the shared heap ----
struct mutex { void *k; };
struct spinlock { void *k; };

extern "C" void shim_mutex_init(struct mutex *m)
	{
	RMutex *x = new RMutex;
	if (!x || x->CreateLocal() != KErrNone)
		shim_panic("mutex");
	m->k = x;
	}
extern "C" void shim_mutex_destroy(struct mutex *m)
	{
	if (m->k)
		{
		((RMutex *)m->k)->Close();
		delete (RMutex *)m->k;
		m->k = NULL;
		}
	}
static RMutex &M(struct mutex *m) { if (!m->k) shim_mutex_init(m); return *(RMutex *)m->k; }
extern "C" void shim_mutex_lock(struct mutex *m) { M(m).Wait(); }
extern "C" void shim_mutex_unlock(struct mutex *m) { M(m).Signal(); }
extern "C" int shim_mutex_trylock(struct mutex *m) { M(m).Wait(); return 1; }

extern "C" void shim_spin_init(struct spinlock *l)
	{
	RFastLock *x = new RFastLock;
	if (!x || x->CreateLocal() != KErrNone)
		shim_panic("spinlock");
	l->k = x;
	}
static RFastLock &S(struct spinlock *l) { if (!l->k) shim_spin_init(l); return *(RFastLock *)l->k; }
extern "C" void shim_spin_lock(struct spinlock *l) { S(l).Wait(); }
extern "C" void shim_spin_unlock(struct spinlock *l) { S(l).Signal(); }

// ---- time ----
static TUint32 FastHz()
	{
	static TInt hz;
	if (!hz)
		HAL::Get(HALData::EFastCounterFrequency, hz);
	return (TUint32)hz;
	}
extern "C" void shim_udelay(unsigned us)		// busy wait: the bit-banged I2C needs microseconds
	{
	const TUint32 start = User::FastCounter();
	const TUint64 ticks = (TUint64)us * FastHz() / 1000000 + 1;
	while ((TUint64)(User::FastCounter() - start) < ticks)
		;
	}
extern "C" void shim_msleep(unsigned ms) { User::AfterHighRes(ms ? ms * 1000 : 1000); }
extern "C" void shim_usleep(unsigned us) { if (us < 1000) shim_udelay(us); else User::AfterHighRes(us); }
extern "C" unsigned long shim_jiffies(void)
	{
	static TInt periodUs;
	if (!periodUs)
		HAL::Get(HALData::ESystemTickPeriod, periodUs);
	return (unsigned long)((TUint64)User::NTickCount() * periodUs / 1000);
	}
extern "C" int64_t shim_ktime_ns(void)
	{
	static TUint64 hi;
	static TUint32 last;
	const TUint32 c = User::FastCounter();	// widened to 64 bits (called often enough not to miss a wrap)
	if (c < last)
		hi += 1ULL << 32;
	last = c;
	return (int64_t)((hi + c) * 1000000000ULL / FastHz());
	}
extern "C" void *shim_current_thread(void) { return (void *)(TUint)RThread().Id(); }

// ---- the CSI register window and DMA memory ----
extern "C" void *phe_hw_map(uint32_t phys, uint32_t size)
	{
	if (phys != 0x01CB0000 || size > 0x1000)
		return NULL;		// the channel maps the CSI and nothing else
	if (!CsiChunk.Handle() && Hw.MapCsi(CsiChunk) != KErrNone)
		return NULL;
	return CsiChunk.Base();
	}

struct TDmaBuf { void *iVirt; RChunk iChunk; };
static TDmaBuf DmaBufs[16];

extern "C" void *phe_hw_dma_alloc(size_t size, uint32_t *phys)
	{
	for (TInt i = 0; i < 16; ++i)
		{
		if (DmaBufs[i].iVirt)
			continue;
		TUint32 pa = 0;
		TInt r = Hw.AllocDma(DmaBufs[i].iChunk, (TUint32)size, pa);
		if (r != KErrNone)
			{
			RDebug::Printf("PH: camera-a64: dma alloc %u -> %d", (TUint)size, r);
			return NULL;
			}
		DmaBufs[i].iVirt = DmaBufs[i].iChunk.Base();
		*phys = pa;
		return DmaBufs[i].iVirt;
		}
	RDebug::Printf("PH: camera-a64: dma alloc %u: all 16 slots used", (TUint)size);
	return NULL;
	}
extern "C" void phe_hw_dma_free(void *virt, size_t, uint32_t)
	{
	for (TInt i = 0; i < 16; ++i)
		if (DmaBufs[i].iVirt == virt)
			{
			DmaBufs[i].iChunk.Close();	// the last handle: the channel's chunk frees the memory
			DmaBufs[i].iVirt = NULL;
			}
	}

// ---- the CSI interrupt: a thread waits on the channel, runs the Linux handler, unmasks ----
static int (*IrqIsr)(int, void *);
static void *IrqCtx;
static TInt IrqNo;

static TInt IrqThread(TAny *)
	{
	for (;;)
		{
		TRequestStatus s;
		Hw.WaitIrq(s);
		User::WaitForRequest(s);
		if (s.Int() != KErrNone)
			return s.Int();
		IrqIsr(IrqNo, IrqCtx);
		Hw.IrqEnable(ETrue);
		}
	}

extern "C" int phe_hw_irq_bind(int irq, int (*isr)(int, void *), void *ctx)
	{
	if (IrqIsr)
		return -28;		// -ENOSPC: one interrupt, the CSI's
	IrqIsr = isr;
	IrqCtx = ctx;
	IrqNo = irq;
	RThread t;
	TInt r = t.Create(_L("CamIrq"), IrqThread, 8192, NULL, NULL);	// NULL heap: this process's shared heap
	if (r != KErrNone)
		return -12;
	t.SetPriority(EPriorityRealTime);
	t.Resume();
	t.Close();
	return 0;
	}
extern "C" void phe_hw_irq_enable(int, int on) { Hw.IrqEnable(on); }

// ---- clocks, reset, GPIOs, rails ----
extern "C" int phe_hw_clk_enable(unsigned int id, int on) { return Hw.Clock(id, on) == KErrNone ? 0 : -22; }
extern "C" unsigned long phe_hw_clk_get_rate(unsigned int id) { TUint32 hz = 0; Hw.ClockRate(id, hz); return hz; }
extern "C" int phe_hw_clk_set_rate(unsigned int id, unsigned long rate) { return Hw.SetClockRate(id, rate) == KErrNone ? 0 : -22; }
extern "C" int phe_hw_reset(unsigned int id, int assert) { return Hw.Reset(id, assert) == KErrNone ? 0 : -22; }

static TUint8 OpenDrain[5][32];
extern "C" int phe_hw_gpio_dir(unsigned int bank, unsigned int pin, int output, int value, int open_drain)
	{
	if (bank >= 5 || pin >= 32)
		return -22;
	OpenDrain[bank][pin] = (TUint8)open_drain;
	TA64CamHwGpio g = { (TUint8)bank, (TUint8)pin,
		(TUint8)(output ? (EA64GpioOutput | (open_drain ? EA64GpioOpenDrain : 0)) : EA64GpioInput), (TUint8)value };
	TInt r = Hw.Gpio(g);
	return r < 0 ? -22 : 0;
	}
extern "C" void phe_hw_gpio_set(unsigned int bank, unsigned int pin, int value)
	{
	if (bank >= 5 || pin >= 32)
		return;
	TA64CamHwGpio g = { (TUint8)bank, (TUint8)pin,
		(TUint8)(EA64GpioOutput | (OpenDrain[bank][pin] ? EA64GpioOpenDrain : 0)), (TUint8)value };
	Hw.Gpio(g);
	}
extern "C" int phe_hw_gpio_get(unsigned int bank, unsigned int pin)
	{
	// reading an open-drain line releases it (input) first, as the Linux open-drain emulation does for a high level
	TA64CamHwGpio g = { (TUint8)bank, (TUint8)pin, (TUint8)(EA64GpioInput | (OpenDrain[bank][pin] ? EA64GpioOpenDrain : 0)), 0 };
	TInt r = Hw.Gpio(g);
	return r < 0 ? 0 : r;
	}

extern "C" int phe_hw_rail(const char *name, int on)
	{
	static const char *const KNames[] = { "reg_aldo1", "reg_dldo3", "reg_eldo3" };	// = TA64Rail order
	for (TInt i = 0; i < 3; ++i)
		if (!shim_strcmp(KNames[i], name))
			return Hw.Rail(i, on) == KErrNone ? 0 : -5;
	return -19;
	}

// ---- firmware: \resource\firmware\<name> (the ov5640-firmware package installs ov5640_af.bin there) ----
extern "C" int phe_hw_firmware(const char *name, const uint8_t **data, size_t *size)
	{
	TBuf<128> path(_L("\\resource\\firmware\\"));
	for (const char *p = name; *p && path.Length() < path.MaxLength(); ++p)
		path.Append((TChar)(TUint8)*p);
	RFs fs;
	if (fs.Connect() != KErrNone)
		return -2;
	TFindFile find(fs);
	TInt r = find.FindByDir(path, KNullDesC);	// any drive, C: before Z:
	RFile f;
	TInt n = 0;
	if (r == KErrNone)
		r = f.Open(fs, find.File(), EFileRead | EFileShareReadersOnly);
	if (r == KErrNone && (r = f.Size(n)) == KErrNone)
		{
		TUint8 *buf = (TUint8 *)User::Alloc(n > 0 ? n : 1);
		TPtr8 des(buf, n);
		r = buf ? f.Read(des) : KErrNoMemory;
		if (r == KErrNone && des.Length() == n)
			{
			*data = buf;
			*size = (size_t)n;
			}
		else
			{
			User::Free(buf);
			r = KErrCorrupt;
			}
		}
	f.Close();
	fs.Close();
	return r == KErrNone ? 0 : -2;		// -ENOENT: the drivers carry on without (ov5640: fixed focus)
	}
extern "C" void phe_hw_firmware_free(const uint8_t *data, size_t) { User::Free((TAny *)data); }
