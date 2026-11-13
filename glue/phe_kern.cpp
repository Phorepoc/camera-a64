// Copyright The Phorepoc Authors
// SPDX-License-Identifier: GPL-2.0-only
//
// phe_kern.cpp -- the kernel side of the shim: the phe_rt.h runtime (allocation, mutexes, spin locks, delays, time,
// logging) and the phe_hw.h hardware calls (MMIO, interrupts, contiguous memory, the A64 CCU and PIO, the AXP803
// rails, firmware) on the Symbian kernel (Kern::, NKern::, Epoc::, DPlatChunkHw, Interrupt::).
//
// Interrupts: a Linux handler takes spin locks and completes vb2 buffers, which may wake waiters, so it does not run
// in the Symbian ISR. The ISR masks the line and queues a DFC on the camera's DFC queue; the DFC runs the Linux
// handler (as a threaded IRQ) and unmasks. Linux spin locks are therefore fast mutexes, taken in thread context only.
//
// The A64 register layouts below are the ones Linux's drivers program (sunxi-ng ccu-sun50i-a64.c, pinctrl-sun50i-a64.c,
// axp20x-regulator.c), restricted to the camera's clocks, pins and rails.

#include <kernel/kern_priv.h>
#include <kernel/kernel.h>
#include <kernel/arm/assp.h>
#include "phe_hw.h"

extern "C" {
#include "../shim/include/phe_rt.h"
TInt A64PmicUpdateReg(TUint8 aReg, TUint8 aClr, TUint8 aSet);	// adaptation-a64 (RSB to the AXP803), exported by ekern
}

// ---- memory ----
extern "C" void *shim_alloc(size_t n) { return Kern::Alloc(n); }
extern "C" void *shim_zalloc(size_t n) { return Kern::AllocZ(n); }
extern "C" void shim_free(void *p) { Kern::Free(p); }
extern "C" void shim_panic(const char *what) { Kern::Fault(what, 0); }

extern "C" void shim_log(const char *fmt, ...)
	{
	char buf[256];
	va_list ap;
	va_start(ap, fmt);
	shim_vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	Kern::Printf("%s", buf);
	}

// ---- locks: struct mutex { void *k; } = a DMutex; struct spinlock { void *k; } = an NFastMutex ----
struct mutex { void *k; };
struct spinlock { void *k; };

extern "C" void shim_mutex_init(struct mutex *m)
	{
	DMutex *dm = NULL;
	_LIT(KName, "PheCamMutex");
	NKern::ThreadEnterCS();
	Kern::MutexCreate(dm, KName, KMutexOrdGeneral1);
	NKern::ThreadLeaveCS();
	m->k = dm;
	}
extern "C" void shim_mutex_destroy(struct mutex *m)
	{
	if (m->k)
		{
		NKern::ThreadEnterCS();
		((DMutex *)m->k)->Close(NULL);
		NKern::ThreadLeaveCS();
		m->k = NULL;
		}
	}
static DMutex *M(struct mutex *m) { if (!m->k) shim_mutex_init(m); return (DMutex *)m->k; }
extern "C" void shim_mutex_lock(struct mutex *m) { NKern::ThreadEnterCS(); Kern::MutexWait(*M(m)); }
extern "C" void shim_mutex_unlock(struct mutex *m) { Kern::MutexSignal(*M(m)); NKern::ThreadLeaveCS(); }
extern "C" int shim_mutex_trylock(struct mutex *m) { shim_mutex_lock(m); return 1; }	// one camera thread: never contended

extern "C" void shim_spin_init(struct spinlock *l) { l->k = Kern::AllocZ(sizeof(NFastMutex)); new (l->k) NFastMutex; }
static NFastMutex *S(struct spinlock *l) { if (!l->k) shim_spin_init(l); return (NFastMutex *)l->k; }
extern "C" void shim_spin_lock(struct spinlock *l) { NKern::FMWait(S(l)); }
extern "C" void shim_spin_unlock(struct spinlock *l) { NKern::FMSignal(S(l)); }

// ---- time ----
extern "C" void shim_udelay(unsigned us) { Kern::NanoWait(us * 1000); }
extern "C" void shim_msleep(unsigned ms)
	{
	TInt ticks = NKern::TimerTicks(ms);
	NKern::Sleep(ticks > 0 ? ticks : 1);
	}
extern "C" void shim_usleep(unsigned us) { if (us < 1000) Kern::NanoWait(us * 1000); else shim_msleep(us / 1000); }
extern "C" unsigned long shim_jiffies(void) { return (unsigned long)((TUint64)NKern::TickCount() * NKern::TickPeriod() / 1000); }
extern "C" int64_t shim_ktime_ns(void)
	{
	TUint64 c = (TUint32)NKern::FastCounter();
	return (int64_t)(c * 1000000000ULL / (TUint32)NKern::FastCounterFrequency());
	}
extern "C" void *shim_current_thread(void) { return NKern::CurrentThread(); }

// ---- MMIO and contiguous memory ----
extern "C" void *phe_hw_map(uint32_t phys, uint32_t size)
	{
	DPlatChunkHw *ch = NULL;
	NKern::ThreadEnterCS();
	TInt r = DPlatChunkHw::New(ch, phys, size, EMapAttrSupRw | EMapAttrFullyBlocking);
	NKern::ThreadLeaveCS();
	return r == KErrNone ? (void *)ch->LinearAddress() : NULL;
	}

struct TDmaBuf { void *iVirt; TPhysAddr iPhys; DPlatChunkHw *iChunk; size_t iSize; };
static TDmaBuf DmaBufs[16];

extern "C" void *phe_hw_dma_alloc(size_t size, uint32_t *phys)
	{
	size = (size + 0xFFF) & ~0xFFFu;
	for (TInt i = 0; i < 16; ++i)
		{
		if (DmaBufs[i].iVirt)
			continue;
		TPhysAddr pa;
		NKern::ThreadEnterCS();
		TInt r = Epoc::AllocPhysicalRam(size, pa, 12);		// contiguous: the CSI has no IOMMU
		DPlatChunkHw *ch = NULL;
		if (r == KErrNone && (r = DPlatChunkHw::New(ch, pa, size, EMapAttrSupRw | EMapAttrFullyBlocking)) != KErrNone)
			Epoc::FreePhysicalRam(pa, size);
		NKern::ThreadLeaveCS();
		if (r != KErrNone)
			return NULL;
		DmaBufs[i].iVirt = (void *)ch->LinearAddress();
		DmaBufs[i].iPhys = pa;
		DmaBufs[i].iChunk = ch;
		DmaBufs[i].iSize = size;
		*phys = pa;
		return DmaBufs[i].iVirt;
		}
	return NULL;
	}

extern "C" void phe_hw_dma_free(void *virt, size_t, uint32_t)
	{
	for (TInt i = 0; i < 16; ++i)
		if (DmaBufs[i].iVirt == virt)
			{
			NKern::ThreadEnterCS();
			DmaBufs[i].iChunk->Close(NULL);
			Epoc::FreePhysicalRam(DmaBufs[i].iPhys, DmaBufs[i].iSize);
			NKern::ThreadLeaveCS();
			DmaBufs[i].iVirt = NULL;
			}
	}

// ---- interrupts: ISR masks + queues a DFC; the DFC runs the Linux handler and unmasks ----
TDfcQue *PheCameraDfcQ();		// a64camerapdd glue (phe_sensor.cpp): the camera's DFC queue

struct TPheIrq
	{
	TInt iIrq;
	int (*iIsr)(int, void *);
	void *iCtx;
	TDfc *iDfc;
	};
static TPheIrq Irqs[4];

static void IrqDfc(TAny *aPtr)
	{
	TPheIrq *h = (TPheIrq *)aPtr;
	h->iIsr(h->iIrq, h->iCtx);
	Interrupt::Enable(h->iIrq);
	}
static void IrqIsr(TAny *aPtr)
	{
	TPheIrq *h = (TPheIrq *)aPtr;
	Interrupt::Disable(h->iIrq);
	h->iDfc->Add();
	}

extern "C" int phe_hw_irq_bind(int irq, int (*isr)(int, void *), void *ctx)
	{
	for (TInt i = 0; i < 4; ++i)
		{
		if (Irqs[i].iIsr)
			continue;
		Irqs[i].iIrq = irq;
		Irqs[i].iIsr = isr;
		Irqs[i].iCtx = ctx;
		NKern::ThreadEnterCS();
		Irqs[i].iDfc = new TDfc(IrqDfc, &Irqs[i], PheCameraDfcQ(), 1);
		NKern::ThreadLeaveCS();
		TInt r = Interrupt::Bind(irq, IrqIsr, &Irqs[i]);
		if (r < 0)
			return -19;	// -ENODEV
		Interrupt::Enable(irq);
		return 0;
		}
	return -28;	// -ENOSPC
	}
extern "C" void phe_hw_irq_enable(int irq, int on) { if (on) Interrupt::Enable(irq); else Interrupt::Disable(irq); }

// ---- A64 CCU (0x01C20000): the camera's gates, dividers and reset ----
static volatile TUint32 *Ccu;
static volatile TUint32 *CcuReg(TUint off)
	{
	if (!Ccu)
		Ccu = (volatile TUint32 *)phe_hw_map(0x01C20000, 0x400);
	return Ccu + off / 4;
	}

// dt-bindings/clock/sun50i-a64-ccu.h indices
enum { KClkBusCsi = 50, KClkDramCsi = 96, KClkCsiMisc = 103, KClkCsiSclk = 104, KClkCsiMclk = 105, KRstBusCsi = 27 };

extern "C" int phe_hw_clk_enable(unsigned int id, int on)
	{
	TUint off, bit;
	switch (id)
		{
		case KClkBusCsi: off = 0x064; bit = 8; break;		// BUS_CLK_GATING_REG1 bit 8
		case KClkDramCsi: off = 0x100; bit = 1; break;		// DRAM_CLK_GATING_REG bit 1
		case KClkCsiMisc: off = 0x130; bit = 31; break;
		case KClkCsiSclk: off = 0x134; bit = 31; break;		// CSI_CLK_REG: SCLK gate
		case KClkCsiMclk: off = 0x134; bit = 15; break;		// CSI_CLK_REG: MCLK gate
		default: return -22;
		}
	volatile TUint32 *r = CcuReg(off);
	*r = on ? (*r | (1u << bit)) : (*r & ~(1u << bit));
	return 0;
	}

extern "C" unsigned long phe_hw_clk_get_rate(unsigned int id)
	{
	TUint32 v = *CcuReg(0x134);
	switch (id)
		{
		case KClkCsiSclk: return 600000000UL / (((v >> 16) & 0xF) + 1);	// pll-periph0 (600 MHz) / (M + 1)
		case KClkCsiMclk: return 24000000UL / ((v & 0x1F) + 1);		// osc24M / (M + 1)
		default: return 0;
		}
	}

extern "C" int phe_hw_clk_set_rate(unsigned int id, unsigned long rate)
	{
	volatile TUint32 *r = CcuReg(0x134);
	TUint32 m;
	switch (id)
		{
		case KClkCsiSclk:	// mux [26:24] = 0 (pll-periph0), M [19:16]
			m = (600000000UL + rate - 1) / rate - 1;
			*r = (*r & ~((7u << 24) | (0xFu << 16))) | ((m & 0xF) << 16);
			return 0;
		case KClkCsiMclk:	// mux [10:8] = 0 (osc24M), M [4:0]
			m = (24000000UL + rate - 1) / rate - 1;
			*r = (*r & ~((7u << 8) | 0x1Fu)) | (m & 0x1F);
			return 0;
		default:
			return 0;	// bus/ram gates: fixed parents
		}
	}

extern "C" int phe_hw_reset(unsigned int id, int assert)
	{
	if (id != KRstBusCsi)
		return -22;
	volatile TUint32 *r = CcuReg(0x2C4);		// BUS_SOFT_RST_REG1 bit 8, 1 = released
	*r = assert ? (*r & ~(1u << 8)) : (*r | (1u << 8));
	return 0;
	}

// ---- A64 PIO (0x01C20800): bank n at n * 0x24; CFG 4 bits per pin, DAT at +0x10 ----
static volatile TUint32 *Pio;
static volatile TUint32 *PioReg(TUint bank, TUint off)
	{
	if (!Pio)
		Pio = (volatile TUint32 *)phe_hw_map(0x01C20800, 0x400);
	return Pio + (bank * 0x24 + off) / 4;
	}
static void PioFunc(TUint bank, TUint pin, TUint func)
	{
	volatile TUint32 *cfg = PioReg(bank, (pin / 8) * 4);
	TUint sh = (pin % 8) * 4;
	*cfg = (*cfg & ~(7u << sh)) | (func << sh);
	}
// open drain is emulated as in Linux's gpiolib: drive low = output 0, release = input (the bus has pull-ups)
static TUint8 OpenDrain[8][32];

extern "C" int phe_hw_gpio_dir(unsigned int bank, unsigned int pin, int output, int value, int open_drain)
	{
	OpenDrain[bank][pin] = (TUint8)open_drain;
	if (output)
		phe_hw_gpio_set(bank, pin, value);
	else
		PioFunc(bank, pin, 0);
	return 0;
	}
extern "C" void phe_hw_gpio_set(unsigned int bank, unsigned int pin, int value)
	{
	volatile TUint32 *dat = PioReg(bank, 0x10);
	if (OpenDrain[bank][pin] && value)
		{
		PioFunc(bank, pin, 0);
		return;
		}
	*dat = value ? (*dat | (1u << pin)) : (*dat & ~(1u << pin));
	PioFunc(bank, pin, 1);
	}
extern "C" int phe_hw_gpio_get(unsigned int bank, unsigned int pin) { return (*PioReg(bank, 0x10) >> pin) & 1; }

// the CSI's own pins (DT csi_pins + csi_mclk_pin: PE0-PE11, function "csi"), set before the CSI is clocked
void PheCameraPinmux()
	{
	for (TUint pin = 0; pin <= 11; ++pin)
		PioFunc(4, pin, 2);
	}

// ---- AXP803 rails (axp20x-regulator: voltage registers and enable bits; DT microvolts) ----
struct TRail { const char *iName; TUint8 iVReg, iVSel, iEnReg, iEnBit; TBool iKeepOn; };
static const TRail Rails[] =
	{
	{ "reg_aldo1", 0x28, 11, 0x13, 5, ETrue },	// 1.8 V (0.7 V + 11 x 0.1); also the PE bank: never switched off here
	{ "reg_dldo3", 0x17, 21, 0x12, 5, ETrue },	// 2.8 V (0.7 V + 21 x 0.1); shared with other consumers
	{ "reg_eldo3", 0x1B, 22, 0x12, 2, EFalse },	// 1.8 V (0.7 V + 22 x 0.05)
	};

extern "C" int phe_hw_rail(const char *name, int on)
	{
	for (TUint i = 0; i < sizeof(Rails) / sizeof(Rails[0]); ++i)
		{
		const TRail &r = Rails[i];
		if (shim_strcmp(r.iName, name))
			continue;
		if (on)
			{
			A64PmicUpdateReg(r.iVReg, 0x1F, r.iVSel);
			return A64PmicUpdateReg(r.iEnReg, 0, (TUint8)(1u << r.iEnBit)) == KErrNone ? 0 : -5;
			}
		if (!r.iKeepOn)
			A64PmicUpdateReg(r.iEnReg, (TUint8)(1u << r.iEnBit), 0);
		return 0;
		}
	return -19;
	}

// ---- firmware: none can be read from the kernel yet; the drivers run without (ov5640: fixed focus) ----
extern "C" int phe_hw_firmware(const char *, const uint8_t **, size_t *) { return -2; }	// -ENOENT
extern "C" void phe_hw_firmware_free(const uint8_t *, size_t) {}
