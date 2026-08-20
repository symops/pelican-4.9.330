/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2017-2019 Realtek Semiconductor Corporation
 * Author: Cheng-Yu Lee <cylee12@realtek.com>
 */

#ifndef __CLK_REALTEK_CLK_PLL_H
#define __CLK_REALTEK_CLK_PLL_H

#include "common.h"

struct freq_table {
	uint32_t val;
	unsigned long rate;
};

#define FREQ_TABLE_END                  { .rate = 0 }
#define IS_FREQ_TABLE_END(_f)           ((_f)->rate == 0)

struct div_table {
	unsigned long rate;
	uint32_t div;
	uint32_t val;
};

#define DIV_TABLE_END                   { .rate = 0 }
#define IS_DIV_TABLE_END(_d)            ((_d)->rate == 0)

struct clk_pll {
	struct clk_regmap clkr;


	int pll_ofs;
	int ssc_ofs;

	const struct freq_table *freq_tbl;
	uint32_t freq_mask;
	uint32_t freq_mask_set;
	uint32_t freq_loc;
#define CLK_PLL_CONF_FREQ_LOC_CTL1      1
#define CLK_PLL_CONF_FREQ_LOC_CTL2      2
#define CLK_PLL_CONF_FREQ_LOC_SSC1      3

	uint32_t pow_loc;
#define CLK_PLL_CONF_NO_POW             0
#define CLK_PLL_CONF_POW_LOC_CTL2       1
#define CLK_PLL_CONF_POW_LOC_CTL3       2
	uint32_t pow_set_rs;
	uint32_t pow_set_pi_bps;
	uint32_t rs_mask;
	uint32_t rs_val;

	spinlock_t *lock;

	uint32_t flags;
};

#define CLK_PLL_CONF_RS(_m, _v) \
	.pow_set_rs = 1,  \
	.rs_mask  = (_m), \
	.rs_val   = (_v)

#define CLK_PLL_CONF_PI_BPS() \
	.pow_set_pi_bps = 1

#define to_clk_pll(_hw) container_of(to_clk_regmap(_hw), struct clk_pll, clkr)
#define __clk_pll_hw(_ptr)  __clk_regmap_hw(&(_ptr)->clkr)

/* clk_pll flags */
#define CLK_PLL_DIV_WORKAROUND          BIT(2)


static inline bool clk_pll_has_pow(struct clk_pll *pll)
{
	if (pll->pow_loc != CLK_PLL_CONF_NO_POW)
		return true;
	return false;
}

static inline unsigned long clk_pll_lock(struct clk_pll *pll)
{
	unsigned long flags = 0;

	if (pll->lock)
		spin_lock_irqsave(pll->lock, flags);
	return flags;
}

static inline void clk_pll_unlock(struct clk_pll *pll, unsigned long flags)
{
	if (pll->lock)
		spin_unlock_irqrestore(pll->lock, flags);
}

struct clk_pll_div {
	struct clk_pll clkp;
	int div_ofs;
	int div_shift;
	int div_width;
	const struct div_table *div_tbl;
	spinlock_t *lock;
};

#define to_clk_pll_div(_hw) \
	container_of(to_clk_pll(_hw), struct clk_pll_div, clkp)
#define __clk_pll_div_hw(_ptr) __clk_pll_hw(&(_ptr)->clkp)

/* clk_pll_div helper functions */
static inline unsigned long clk_pll_div_lock(struct clk_pll_div *plld)
{
	unsigned long flags = 0;

	if (plld->lock)
		spin_lock_irqsave(plld->lock, flags);
	return flags;
}

static inline void clk_pll_div_unlock(struct clk_pll_div *plld,
	unsigned long flags)
{
	if (plld->lock)
		spin_unlock_irqrestore(plld->lock, flags);
}

extern const struct clk_ops clk_pll_ops;
extern const struct clk_ops clk_pll_div_ops;

#ifdef CONFIG_CLK_PLL_PSAUD

struct clk_pll_psaud {
	struct clk_regmap clkr;
	int id;
	int reg;
	spinlock_t *lock;
};

#define to_clk_pll_psaud(_hw) \
	container_of(to_clk_regmap(_hw), struct clk_pll_psaud, clkr)
#define __clk_pll_psaud_hw(_ptr)  __clk_regmap_hw(&(_ptr)->clkr)
extern const struct clk_ops clk_pll_psaud_ops;

#define CLK_PLL_PSAUD1A       (0x1)
#define CLK_PLL_PSAUD2A       (0x2)

#endif /* CONFIG_CLK_PLL_PSAUD */

#ifdef CONFIG_CLK_PLL_DIF

struct clk_pll_dif {
	struct clk_regmap clkr;
	int pll_ofs;
	int ssc_ofs;
	uint32_t status;
	spinlock_t *lock;
	uint32_t adtv_conf[8];
};
#define to_clk_pll_dif(_hw) \
	container_of(to_clk_regmap(_hw), struct clk_pll_dif, clkr)
#define __clk_pll_dif_hw(_ptr)  __clk_regmap_hw(&(_ptr)->clkr)

extern const struct clk_ops clk_pll_dif_ops;
#endif /* CONFIG_CLK_PLL_DIF */

#ifdef CONFIG_CLK_PLL_EDP

struct clk_pll_edp {
	struct clk_regmap clkr;
	int pll_ofs;
	int ssc_ofs;
	uint32_t status;
	spinlock_t *lock;
};
#define to_clk_pll_edp(_hw) \
    container_of(to_clk_regmap(_hw), struct clk_pll_edp, clkr)
#define __clk_pll_edp_hw(_ptr)  __clk_regmap_hw(&(_ptr)->clkr)

extern const struct clk_ops clk_pll_edp_ops;
#endif /* CONFIG_CLK_PLL_EDP */

#ifdef CONFIG_CLK_PLL_PIXEL

struct clk_pll_pixel {
	struct clk_regmap clkr;
	int pll_ofs;
	int ssc_ofs;
	uint32_t status;
	spinlock_t *lock;
};
#define to_clk_pll_pixel(_hw) \
    container_of(to_clk_regmap(_hw), struct clk_pll_pixel, clkr)
#define __clk_pll_pixel_hw(_ptr)  __clk_regmap_hw(&(_ptr)->clkr)

extern const struct clk_ops clk_pll_pixel_ops;
#endif /* CONFIG_CLK_PLL_PIXEL */

#endif /* __CLK_REALTEK_CLK_PLL_H */
