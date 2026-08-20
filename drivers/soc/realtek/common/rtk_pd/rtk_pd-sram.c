// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2017-2020 Realtek Semiconductor Corporation
 * Author: Cheng-Yu Lee <cylee12@realtek.com>
 */

#include <linux/spinlock.h>
#include "rtk_pd.h"

#define SRAM_PWR0 0x0
#define SRAM_PWR1 0x4
#define SRAM_PWR2 0x8
#define SRAM_PWR3 0xC
#define SRAM_PWR4 0x10
#define SRAM_PWR5 0x14
#define SRAM_PWR6 0x18

static inline unsigned long rtk_pd_sram_lock(struct rtk_pd_sram *spd)
{
	unsigned long flags = 0;

	if (spd->lock)
		spin_lock_irqsave(spd->lock, flags);
	return flags;
}

static inline void rtk_pd_sram_unlock(struct rtk_pd_sram *spd, unsigned long flags)
{
	if (spd->lock)
		spin_unlock_irqrestore(spd->lock, flags);
}

static unsigned int rtk_pd_sram_pwr5_offset(struct rtk_pd_sram *spd)
{
	return spd->pwr5_offset ?: (spd->pwr_offset + SRAM_PWR5);
}

static void rtk_pd_sram_clear_ints(struct rtk_pd_sram *spd)
{
	unsigned int pwr5 = rtk_pd_sram_pwr5_offset(spd);

	rtk_pd_device_reg_write(spd->core.pd_dev, pwr5, 0x4);
}

static int rtk_pd_sram_poll_ints(struct rtk_pd_sram *spd)
{
	unsigned int pwr5 = rtk_pd_sram_pwr5_offset(spd);
	unsigned int pollval;

	return regmap_read_poll_timeout(spd->core.pd_dev->regmap, pwr5,
		pollval, pollval == 0x4, 0, 500);
}

static int rtk_pd_sram_set_power(struct rtk_pd_sram *spd, int on_off)
{
	unsigned int pwr4 = spd->pwr_offset + SRAM_PWR4;
	unsigned int val = on_off ? spd->val_on : spd->val_off;
	unsigned int reg;

	rtk_pd_device_reg_read(spd->core.pd_dev, pwr4, &reg);
	if ((reg & 0xff) == val)
		return 1;
	val |= spd->last_sd_ch << 8;
	rtk_pd_device_reg_write(spd->core.pd_dev, pwr4, val);

	return rtk_pd_sram_poll_ints(spd);
}

static int rtk_pd_sram_power_on(struct rtk_pd *pd)
{
	struct rtk_pd_sram *spd = rtk_pd_to_sram(pd);
	unsigned long flags;
	int ret;

	pr_debug("%s: %s\n", rtk_pd_name(pd), __func__);

	flags = rtk_pd_sram_lock(spd);

	ret = rtk_pd_sram_set_power(spd, 1);
	if (ret < 0)
		pr_warn("%s: power_on error: %d\n", rtk_pd_name(pd), ret);
	rtk_pd_sram_clear_ints(spd);

	rtk_pd_sram_unlock(spd, flags);
	return 0;
}

static int rtk_pd_sram_power_off(struct rtk_pd *pd)
{
	struct rtk_pd_sram *spd = rtk_pd_to_sram(pd);
	unsigned long flags;
	int ret;

	pr_debug("%s: %s\n", rtk_pd_name(pd), __func__);

	flags = rtk_pd_sram_lock(spd);

	ret = rtk_pd_sram_set_power(spd, 0);
	if (ret < 0)
		pr_warn("%s: power_off error: %d\n", rtk_pd_name(pd), ret);
	rtk_pd_sram_clear_ints(spd);

	rtk_pd_sram_unlock(spd, flags);

	return 0;
}

static int rtk_pd_sram_power_state(struct rtk_pd *pd)
{
	struct rtk_pd_sram *spd = rtk_pd_to_sram(pd);
	unsigned long flags;
	unsigned int val;
	unsigned int pwr4 = spd->pwr_offset + SRAM_PWR4;

	flags = rtk_pd_sram_lock(spd);
	rtk_pd_device_reg_read(spd->core.pd_dev, pwr4, &val);
	rtk_pd_sram_unlock(spd, flags);
	return (val & 0xff) == spd->val_on;
}

const struct rtk_pd_ops rtk_pd_sram_ops = {
	.power_off   = rtk_pd_sram_power_off,
	.power_on    = rtk_pd_sram_power_on,
	.power_state = rtk_pd_sram_power_state,
};

