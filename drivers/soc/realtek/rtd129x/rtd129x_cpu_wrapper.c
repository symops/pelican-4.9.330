/*
 * rtd129x_cpu_wrapper.c
 *
 * Copyright (c) 2017 Realtek Semiconductor Corp.
 *
 * Author: Chih-Feng Tai <james.tai@realtek.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/printk.h>
#include <linux/delay.h>
#include <linux/smp.h>
#include <linux/types.h>
#include <linux/ioport.h>
#include <linux/io.h>
#include <linux/memblock.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/of_irq.h>
#include <asm/cacheflush.h>
#include <asm/cpu_ops.h>
#include <asm/cputype.h>
#include <asm/smp_plat.h>
#include <asm/system_misc.h>

#define DEV_NAME "RTK_SCPU_WRAPPER"

#define CR_M (1 << 0) /* MMU enable */
#define CR_C (1 << 2) /* D-cache enable */

static void __iomem *scpu_wrap_addr;

static inline unsigned int current_el(void)
{
	unsigned int el;

	asm volatile("mrs %0, CurrentEL" : "=r" (el) : : "cc");
	return el >> 2;
}

static inline void set_sctlr(unsigned int val)
{
	unsigned int el = current_el();

	if (el == 1)
		asm volatile("msr sctlr_el1, %0" : : "r" (val) : "cc");
	else if (el == 2)
		asm volatile("msr sctlr_el2, %0" : : "r" (val) : "cc");
	else
		asm volatile("msr sctlr_el3, %0" : : "r" (val) : "cc");

	asm volatile("isb");
}

static inline unsigned int get_sctlr(void)
{
	unsigned int el = current_el();
	unsigned int val;

	if (el == 1)
		asm volatile("mrs %0, sctlr_el1" : "=r" (val) : : "cc");
	else if (el == 2)
		asm volatile("mrs %0, sctlr_el2" : "=r" (val) : : "cc");
	else
		asm volatile("mrs %0, sctlr_el3" : "=r" (val) : : "cc");
	return val;
}

void dcache_disable(void)
{
	u32 sctlr = get_sctlr();

	/* if cache isn't enabled no need to disable */
	if (!(sctlr & CR_C) && !(sctlr & CR_M))
		return;

	set_sctlr(sctlr & ~(CR_C|CR_M));
}

void _rtk_cpu_power_up(int cpu)
{
	u32 tmp = 0;

	/* 5. Wait for SMPEN to be set. */
	tmp = readl(scpu_wrap_addr + 0x104);
	if (unlikely(((tmp >> (16 + cpu)) & 1) != 1)) {
		WARN_ON(1);
		pr_err("Waiting for CPU%d SMPEN to be set\n", cpu);
	}

	/*
	 * 6. Assert DBGPWRDUP HIGH to
	 * allow external debug access to the core.
	 */
	tmp = readl(scpu_wrap_addr + 0x100);
	tmp |= 1UL << (24 + cpu);
	writel(tmp, scpu_wrap_addr + 0x100);
}
EXPORT_SYMBOL(_rtk_cpu_power_up);

void rtk_cpu_power_up(int cpu)
{
	u32 tmp = 0;

	/*
	 * 1. Assert nCPUPORESET LOW.
	 * Ensure DBGPWRDUP is held LOW to prevent any
	 * external debug access to the core.
	 */
	tmp = readl(scpu_wrap_addr + 0x100);
	tmp &= ~(1UL << (24 + cpu));
	tmp &= ~(1UL << (4 + cpu));
	tmp &= ~(1UL << (0 + cpu));
	writel(tmp, scpu_wrap_addr + 0x100);

	/*
	 * 2. Apply power to the PDCPU power domain.
	 * Keep the state of the signals nCPUPORESET and DBGPWRDUP LOW.
	 */
	tmp = readl(scpu_wrap_addr + 0x530);
	tmp |= 1UL << (8 + cpu);
	writel(tmp, scpu_wrap_addr + 0x530);
	//mdelay(3);
	tmp = readl(scpu_wrap_addr + 0x530);
	tmp |= 1UL << (12 + cpu);
	writel(tmp, scpu_wrap_addr + 0x530);

	tmp = readl(scpu_wrap_addr + 0x534);
	if ((unlikely((tmp >> cpu) & 0x1) == 0x1)) {
		WARN_ON(1);
		pr_err("[%s] L1 cache is in power-saving mode.\n", DEV_NAME);
		pr_err("[%s] 0x9801D534 = %x\n",
			DEV_NAME,
			readl(scpu_wrap_addr + 0x534));
	}

	/* 3. Release the core output clamps */
	tmp = readl(scpu_wrap_addr + 0x530);
	tmp &= ~(1UL << (0 + cpu));
	writel(tmp, scpu_wrap_addr + 0x530);

	/* 4. Dessert resets: nCOREPORESET, nCORERESET */
	tmp = readl(scpu_wrap_addr + 0x100);
	tmp |= 1UL << (0 + cpu);
	tmp |= 1UL << (4 + cpu);
	writel(tmp, scpu_wrap_addr + 0x100);
}
EXPORT_SYMBOL(rtk_cpu_power_up);

void rtk_cpu_power_down(int cpu)
{
	u32 tmp = 0;

	/*
	 * Step 1 to 6 is done in rtd129x_cpu_hotplug.S
	 * Wait for WFI in step 6.
	 */
	mdelay(5);
	tmp = readl(scpu_wrap_addr + 0x104);
	if (unlikely(((tmp >> cpu) & 0x1) != 0x1)) {
		pr_err("[%s] Processor is not in WFI standby mode.\n",
			DEV_NAME);
		pr_err("[%s] 0x9801D104 = %x\n",
			DEV_NAME,
			readl(scpu_wrap_addr + 0x104));
	}

	/*
	 * 7. Dessert DBGPWRDUP LOW.
	 * This prevents any external debug access to the core.
	 */
	tmp = readl(scpu_wrap_addr + 0x100);
	tmp &= ~(1UL << (24 + cpu));
	writel(tmp, scpu_wrap_addr + 0x100);

	/* 8. Activate the core output clamps.*/
	tmp = readl(scpu_wrap_addr + 0x530);
	tmp |= 1UL << (0 + cpu);
	writel(tmp, scpu_wrap_addr + 0x530);

#if 0
	/* Assert nCPUPORESET LOW */
	tmp = readl(scpu_wrap_addr + 0x100);
	tmp &= ~(1UL << (0 + cpu));
	tmp &= ~(1UL << (4 + cpu));
	writel(tmp, scpu_wrap_addr + 0x100);
#endif

	/* 9. Remove power from the PDCPU power domain. */
	tmp = readl(scpu_wrap_addr + 0x530);
	tmp &= ~(1UL << (8 + cpu));
	tmp &= ~(1UL << (12 + cpu));
	writel(tmp, scpu_wrap_addr + 0x530);
}
EXPORT_SYMBOL(rtk_cpu_power_down);

static const struct of_device_id rtk_scpu_wrapper_ids[] __initconst = {
	{.compatible = "Realtek,rtk-scpu_wrapper"},
	{},
};

static int __init scpu_wrapper_init(void)
{
	struct device_node *np;

	np = of_find_matching_node(NULL, rtk_scpu_wrapper_ids);
	if (unlikely(np == NULL))
		return -1;

	scpu_wrap_addr = of_iomap(np, 0);
	if (!scpu_wrap_addr)
		return -ENOMEM;

	of_node_put(np);

	return 0;
}
arch_initcall(scpu_wrapper_init);
