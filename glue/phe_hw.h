/* Copyright The Phorepoc Authors
 * SPDX-License-Identifier: GPL-2.0-only */

/* phe_hw.h -- the boundary between the C side (vendored drivers + shim/rt) and the Symbian kernel side (glue/*.cpp).
 * Everything the shim cannot do in portable C goes through these few calls: MMIO mapping, interrupts, physically
 * contiguous memory, the A64 clock/reset gates, GPIO pins, the PMIC rails, firmware files. */
#ifndef PHE_HW_H
#define PHE_HW_H
#include <stdint.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif

void *phe_hw_map(uint32_t phys, uint32_t size);				/* uncached kernel mapping, NULL = fail */
int phe_hw_irq_bind(int irq, int (*isr)(int irq, void *ctx), void *ctx);	/* GIC SPI number + 32 */
void phe_hw_irq_enable(int irq, int on);
void *phe_hw_dma_alloc(size_t size, uint32_t *phys);			/* contiguous, uncached */
void phe_hw_dma_free(void *virt, size_t size, uint32_t phys);

/* clocks / resets by the DT's ccu index (CLK_BUS_CSI, CLK_CSI_SCLK, CLK_DRAM_CSI, CLK_CSI_MCLK, RST_BUS_CSI) */
int phe_hw_clk_enable(unsigned int id, int on);
unsigned long phe_hw_clk_get_rate(unsigned int id);
int phe_hw_clk_set_rate(unsigned int id, unsigned long rate);
int phe_hw_reset(unsigned int id, int assert);

/* GPIO by (bank, pin), as in <&pio bank pin flags>; the pin's DT flags are applied by the shim */
int phe_hw_gpio_dir(unsigned int bank, unsigned int pin, int output, int value, int open_drain);
void phe_hw_gpio_set(unsigned int bank, unsigned int pin, int value);
int phe_hw_gpio_get(unsigned int bank, unsigned int pin);

/* PMIC rails by the DT regulator node name ("reg_aldo1", "reg_dldo3", "reg_eldo3") */
int phe_hw_rail(const char *name, int on);

/* firmware: a file the user put on the phone; -ENOENT when absent (the drivers then run without it) */
int phe_hw_firmware(const char *name, const uint8_t **data, size_t *size);
void phe_hw_firmware_free(const uint8_t *data, size_t size);

#ifdef __cplusplus
}
#endif
#endif
