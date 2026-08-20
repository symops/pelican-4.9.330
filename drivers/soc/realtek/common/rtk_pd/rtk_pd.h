/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2017-2020 Realtek Semiconductor Corporation
 * Author: Cheng-Yu Lee <cylee12@realtek.com>
 */
#ifndef __SOC_REALTEK_PD_H
#define __SOC_REALTEK_PD_H

#include <linux/pm_runtime.h>
#include <linux/pm_domain.h>
#include <linux/spinlock.h>
#include <linux/regmap.h>
#include <linux/list.h>

struct rtk_pd_device {
	struct regmap *regmap;
	struct list_head list;
	struct device *dev;

	struct generic_pm_domain **domains;
	int num_domains;
	struct genpd_onecell_data of_provider_data;
};

static inline int rtk_pd_device_reg_read(struct rtk_pd_device *pd_dev, unsigned int offset, unsigned int *val)
{
	return regmap_read(pd_dev->regmap, offset, val);
}

static inline int rtk_pd_device_reg_write(struct rtk_pd_device *pd_dev, unsigned int offset, unsigned int val)
{
	return regmap_write(pd_dev->regmap, offset, val);
}

static inline int rtk_pd_device_reg_update_bits(struct rtk_pd_device *pd_dev,
	unsigned int offset, unsigned int mask, unsigned int val)
{
	return regmap_update_bits(pd_dev->regmap, offset, mask, val);
}

extern const struct dev_pm_ops rtk_pd_generic_pm_ops;

struct rtk_pd;

struct rtk_pd_ops {
	int (*power_on)(struct rtk_pd *);
	int (*power_off)(struct rtk_pd *);
	int (*power_state)(struct rtk_pd *);
};

struct rtk_pd {
	struct generic_pm_domain pd;
	struct rtk_pd_device *pd_dev;
	const struct rtk_pd_ops *ops;
	struct list_head list;
	unsigned long flags;
};

#define RTK_PD_FLAGS_ALWAYS_ON    1

static inline struct dev_power_governor *rtk_pd_get_gov(struct rtk_pd *pd)
{
	if (pd->flags & RTK_PD_FLAGS_ALWAYS_ON)
		return &pm_domain_always_on_gov;
	return NULL;
}

#define gen_pd_to_rtk_pd(_pd) container_of(_pd, struct rtk_pd, pd)

#define rtk_pd_name(_pd)  ((_pd)->pd.name)
int rtk_pd_init(struct rtk_pd_device *, struct rtk_pd *);
int rtk_pd_device_add_domains(struct rtk_pd_device *pd_dev, struct generic_pm_domain **domains, int num_domains);
void rtk_pd_device_show_power_state(struct rtk_pd_device *pd_dev);
int rtk_pd_setup_power_tree(struct rtk_pd_device *pd_dev, int map[][2], int num_maps);

static inline int rtk_pd_power_on(struct rtk_pd *pd)
{
	if (pd->ops && pd->ops->power_on)
		return pd->ops->power_on(pd);
	return 0;
}

static inline int rtk_pd_power_off(struct rtk_pd *pd)
{
	if (pd->ops && pd->ops->power_off)
		return pd->ops->power_off(pd);
	return 0;
}

static inline int rtk_pd_power_state(struct rtk_pd *pd)
{
	if (pd->ops && pd->ops->power_state)
		return pd->ops->power_state(pd);
	return 1;
}

#define INIT_RTK_PD(_name, _flags, _ops) \
{                                \
	.pd = {                  \
		.name  = _name,  \
		.flags = _flags, \
	},                       \
	.ops     = _ops,         \
}

#define INIT_RTK_PD_EXT(_name, _flags, _ops, _ext_flags) \
{                                \
	.pd = {                  \
		.name  = _name,  \
		.flags = _flags, \
	},                       \
	.ops     = _ops,         \
	.flags   = _ext_flags,   \
}

struct rtk_pd_sram {
	struct rtk_pd core;
	spinlock_t *lock;
	unsigned int pwr_offset;
	unsigned int pwr5_offset;
	unsigned int last_sd_ch;
	unsigned int val_on;
	unsigned int val_off;
};

#define rtk_pd_to_sram(pd) container_of(pd, struct rtk_pd_sram, core)
extern const struct rtk_pd_ops rtk_pd_sram_ops;

#define INIT_RTK_PD_SRAM_COMM(_name, _off, _off_pwr5, _ch, _val_on, _val_off, _lock) \
{                                                               \
	.core        = INIT_RTK_PD(_name, 0, &rtk_pd_sram_ops), \
	.lock        = _lock,                                   \
	.pwr_offset  = _off,                                    \
	.pwr5_offset = _off_pwr5,                               \
	.last_sd_ch  = _ch,                                     \
	.val_on      = _val_on,                                 \
	.val_off     = _val_off,                                \
}

#define INIT_RTK_PD_SRAM(_name, _off, _ch, _lock) \
	INIT_RTK_PD_SRAM_COMM(_name, _off, 0, _ch, 0, 1, _lock)
#define INIT_RTK_PD_SRAM_NCONT(_name, _off, _off_pwr5, _ch, _lock) \
	INIT_RTK_PD_SRAM_COMM(_name, _off, _off_pwr5, _ch, 0, 1, _lock)

struct rtk_pd_simple {
	struct rtk_pd core;
	spinlock_t *lock;
	unsigned int offset;
	unsigned int mask;
	unsigned int val_on;
	unsigned int val_off;
};

#define rtk_pd_to_simple(pd) container_of(pd, struct rtk_pd_simple, core)
extern const struct rtk_pd_ops rtk_pd_simple_ops;

#define INIT_RTK_PD_SIMPLE(_name, _off, _val_on, _val_off, _mask, _lock) \
{                                                             \
	.core    = INIT_RTK_PD(_name, 0, &rtk_pd_simple_ops), \
	.offset  = _off,                                      \
	.mask    = _mask,                                     \
	.val_on  = 0,                                         \
	.val_off = _mask,                                     \
	.lock    = _lock,                                     \
}

#define INIT_RTK_PD_ISO(_name, _off, _bit, _lock) \
	INIT_RTK_PD_SIMPLE(_name, _off, 0, BIT(_bit), BIT(_bit), _lock)

#endif
