// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2019-2020 Realtek Semiconductor Corporation
 * Author: Cheng-Yu Lee <cylee12@realtek.com>
 */

#include <linux/spinlock.h>
#include "rtk_pd.h"

static inline unsigned long rtk_pd_simple_lock(struct rtk_pd_simple *spd)
{
	unsigned long flags = 0;

	if (spd->lock)
		spin_lock_irqsave(spd->lock, flags);
	return flags;
}

static inline void rtk_pd_simple_unlock(struct rtk_pd_simple *spd, unsigned long flags)
{
	if (spd->lock)
		spin_unlock_irqrestore(spd->lock, flags);
}

static int  rtk_pd_simple_set_power(struct rtk_pd_simple *spd, int on_off)
{
	unsigned int val = on_off ? spd->val_on : spd->val_off;

	return rtk_pd_device_reg_update_bits(spd->core.pd_dev, spd->offset,
		spd->mask, val);
}

static int rtk_pd_simple_power_on(struct rtk_pd *pd)
{
	struct rtk_pd_simple *spd = rtk_pd_to_simple(pd);
	unsigned long flags;

	pr_debug("%s: %s\n", rtk_pd_name(pd), __func__);

	flags = rtk_pd_simple_lock(spd);
	rtk_pd_simple_set_power(spd, 1);
	rtk_pd_simple_unlock(spd, flags);

	return 0;
}

static int rtk_pd_simple_power_off(struct rtk_pd *pd)
{
	struct rtk_pd_simple *spd = rtk_pd_to_simple(pd);
	unsigned long flags;

	pr_debug("%s: %s\n", rtk_pd_name(pd), __func__);

	flags = rtk_pd_simple_lock(spd);
	rtk_pd_simple_set_power(spd, 0);
	rtk_pd_simple_unlock(spd, flags);

	return 0;
}

static int rtk_pd_simple_power_state(struct rtk_pd *pd)
{
	struct rtk_pd_simple *spd = rtk_pd_to_simple(pd);
	unsigned long flags;
	unsigned int val;

	flags = rtk_pd_simple_lock(spd);
	rtk_pd_device_reg_read(spd->core.pd_dev, spd->offset, &val);
	rtk_pd_simple_unlock(spd, flags);

	return (val & spd->mask) == (spd->val_on & spd->mask);
}

const struct rtk_pd_ops rtk_pd_simple_ops = {
	.power_off   = rtk_pd_simple_power_off,
	.power_on    = rtk_pd_simple_power_on,
	.power_state = rtk_pd_simple_power_state,
};
