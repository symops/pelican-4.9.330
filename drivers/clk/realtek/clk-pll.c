// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2017-2018 Realtek Semiconductor Corporation
 * Author: Cheng-Yu Lee <cylee12@realtek.com>
 */

#include <linux/io.h>
#include <linux/slab.h>
#include <linux/clk-provider.h>
#include <linux/clk.h>
#include <linux/spinlock.h>
#include <linux/delay.h>
#include <linux/debugfs.h>
#include "common.h"
#include "clk-pll.h"

static const struct freq_table *ftbl_find_by_rate(const struct freq_table *ftbl,
						  unsigned long rate)
{
	unsigned long best_rate = 0;
	const struct freq_table *best = NULL;

	for ( ; !IS_FREQ_TABLE_END(ftbl); ftbl++) {
		if (ftbl->rate == rate)
			return ftbl;

		if (ftbl->rate > rate)
			continue;

		if ((rate - best_rate) > (rate - ftbl->rate)) {
			best_rate = ftbl->rate;
			best = ftbl;
		}
	}

	return best;
}

static const struct freq_table *ftbl_find_by_val_masked(const struct freq_table *ftbl,
							uint32_t mask, uint32_t value)
{
	while (!IS_FREQ_TABLE_END(ftbl)) {
		if ((ftbl->val & mask) == (value & mask))
			return ftbl;
		ftbl++;
	}
	return NULL;
};

static const struct div_table *dtbl_find_by_rate(const struct div_table *dtbl,
						 unsigned long rate)
{
	while (!IS_DIV_TABLE_END(dtbl)) {
		if (rate >= dtbl->rate)
			return dtbl;
		dtbl++;
	}
	return NULL;
}

static const struct div_table *dtbl_find_by_val(const struct div_table *dtbl,
						uint32_t val)
{
	while (!IS_DIV_TABLE_END(dtbl)) {
		if (val == dtbl->val)
			return dtbl;
		dtbl++;
	}
	return NULL;
}

static inline int __set_oc_en(struct clk_pll *clkp)
{
	uint32_t val;
	uint32_t pollval;

	val = clk_regmap_read(&clkp->clkr, clkp->ssc_ofs + 0x0);
	if (val == 0x5)
		return 0;
	clk_regmap_update(&clkp->clkr, clkp->ssc_ofs + 0x0, 0x7, 0x5);

	return regmap_read_poll_timeout(clkp->clkr.regmap, clkp->ssc_ofs + 0x1c,
		pollval, pollval & BIT(20), 0, 2000);
}

static inline int __set_pow_on(struct clk_pll *clkp)
{
	struct clk_regmap *clkr = &clkp->clkr;
	uint32_t pow = (clkp->pow_loc == CLK_PLL_CONF_POW_LOC_CTL3) ? 0x8 : 0x4;

	if (clkp->pow_set_rs)
		clk_regmap_update(clkr, clkp->pll_ofs, clkp->rs_mask, clkp->rs_val);

	clk_regmap_update(clkr, clkp->pll_ofs + pow, 0x7, 0x5);
	clk_regmap_update(clkr, clkp->pll_ofs + pow, 0x7, 0x7);
	udelay(200);

	if (clkp->pow_set_pi_bps)
		clk_regmap_update(clkr, clkp->pll_ofs, 0x10, 0);

	clk_regmap_update(clkr, clkp->pll_ofs + pow, 0x7, 0x3);

	return clkp->freq_loc == CLK_PLL_CONF_FREQ_LOC_SSC1 ? __set_oc_en(clkp) : 0;
}

static inline int __set_pow_off(struct clk_pll *clkp)
{
	struct clk_regmap *clkr = &clkp->clkr;
	uint32_t pow = (clkp->pow_loc == CLK_PLL_CONF_POW_LOC_CTL3) ? 0x8 : 0x4;

	clk_regmap_update(clkr, clkp->pll_ofs + pow, 0x7, 0x4);
	if (clkp->freq_loc != CLK_PLL_CONF_FREQ_LOC_SSC1)
		return 0;

	if (clkp->pow_set_pi_bps)
		clk_regmap_update(clkr, clkp->pll_ofs, 0x10, 0x10);
	return 0;
}

static inline int __get_pow(struct clk_pll *clkp)
{
	uint32_t pow = (clkp->pow_loc == CLK_PLL_CONF_POW_LOC_CTL3) ? 0x8 : 0x4;
	uint32_t val;

	pow = (clkp->pow_loc == CLK_PLL_CONF_POW_LOC_CTL3) ? 0x8 : 0x4;
	val = clk_regmap_read(&clkp->clkr, clkp->pll_ofs + pow);
	return !!(val & 0x1);
}

static int clk_pll_enable(struct clk_hw *hw)
{
	struct clk_pll *clkp = to_clk_pll(hw);
	int ret = 0;

	if (clk_pll_has_pow(clkp))
		ret = __set_pow_on(clkp);
	if (ret)
		pr_warn("%pC: error in %s: %d\n", hw->clk, __func__, ret);
	return 0;
}

static void clk_pll_disable(struct clk_hw *hw)
{
	struct clk_pll *clkp = to_clk_pll(hw);
	int ret = 0;

	if (clk_pll_has_pow(clkp))
		ret = __set_pow_off(clkp);
	if (ret)
		pr_warn("%pC: error in %s: %d\n", hw->clk, __func__, ret);
}

static void clk_pll_disable_unused(struct clk_hw *hw)
{
	pr_info("%pC: %s\n", hw->clk, __func__);
	clk_pll_disable(hw);
}

static int clk_pll_is_enabled(struct clk_hw *hw)
{
	struct clk_pll *clkp = to_clk_pll(hw);

	if (!clk_pll_has_pow(clkp))
		return -EINVAL;

	return __get_pow(clkp);
}

static long clk_pll_round_rate(struct clk_hw *hw, unsigned long rate,
			       unsigned long *parent_rate)
{
	struct clk_pll *clkp = to_clk_pll(hw);
	const struct freq_table *ftblv = NULL;

	ftblv = ftbl_find_by_rate(clkp->freq_tbl, rate);
	return ftblv ? ftblv->rate : 0;
}

static uint32_t __clk_pll_freq_raw_get(struct clk_pll *clkp)
{
	uint32_t val = 0;

	switch (clkp->freq_loc) {
	case CLK_PLL_CONF_FREQ_LOC_CTL1:
		val = clk_regmap_read(&clkp->clkr, clkp->pll_ofs + 0x0);
		break;

	case CLK_PLL_CONF_FREQ_LOC_CTL2:
		val = clk_regmap_read(&clkp->clkr, clkp->pll_ofs + 0x4);
		break;

	case CLK_PLL_CONF_FREQ_LOC_SSC1:
		val = clk_regmap_read(&clkp->clkr, clkp->ssc_ofs + 0x4);
		break;

	default:
		break;
	}
	return val;
}

static unsigned long clk_pll_recalc_rate(struct clk_hw *hw,
					 unsigned long parent_rate)
{
	struct clk_pll *clkp = to_clk_pll(hw);
	unsigned long flags = 0;
	const struct freq_table *fv;
	uint32_t raw;

	flags = clk_pll_lock(clkp);
	raw = __clk_pll_freq_raw_get(clkp);
	clk_pll_unlock(clkp, flags);

	fv = ftbl_find_by_val_masked(clkp->freq_tbl, clkp->freq_mask, raw);
	return fv ? fv->rate : 0;
}

static inline int __clk_pll_freq_set(struct clk_pll *clkp, uint32_t val)
{
	struct clk_hw *hw = &clkp->clkr.hw;
	int ret = 0;
	uint32_t mask = clkp->freq_mask_set ?: clkp->freq_mask;

	switch (clkp->freq_loc) {
	case CLK_PLL_CONF_FREQ_LOC_CTL1:
		clk_regmap_update(&clkp->clkr, clkp->pll_ofs, mask, val);
		break;

	case CLK_PLL_CONF_FREQ_LOC_CTL2:
		clk_regmap_update(&clkp->clkr, clkp->pll_ofs + 0x4, mask, val);
		if (clk_pll_is_enabled(hw) > 0) {
			__set_pow_off(clkp);
			__set_pow_on(clkp);
		}
		break;

	case CLK_PLL_CONF_FREQ_LOC_SSC1:

		clk_regmap_update(&clkp->clkr, clkp->ssc_ofs + 0x0, 0x7, 0x4);
		clk_regmap_update(&clkp->clkr, clkp->ssc_ofs + 0x4, mask, val);

		if (clk_pll_is_enabled(hw) == 0)
			break;
		ret = __set_oc_en(clkp);
		if (ret)
			pr_warn("%pC: %s: error to set_oc: %d\n", hw->clk, __func__, ret);
		ret = 0;
		break;

	default:
		ret = -EINVAL;
		break;
	}
	return ret;
}

static int clk_pll_set_rate(struct clk_hw *hw, unsigned long rate,
			    unsigned long parent_rate)
{
	struct clk_pll *clkp = to_clk_pll(hw);
	unsigned long flags = 0;
	const struct freq_table *fv;
	int ret = 0;

	fv = ftbl_find_by_rate(clkp->freq_tbl, rate);
	if (!fv)
		return -EINVAL;

	pr_debug("%pC: %s: rate=%ld, val=0x%08x\n", hw->clk, __func__,
		 fv->rate, fv->val);

	flags = clk_pll_lock(clkp);
	ret = __clk_pll_freq_set(clkp, fv->val);
	clk_pll_unlock(clkp, flags);
	if (ret)
		pr_warn("%pC %s: failed to set freq: %d\n", hw->clk, __func__,
			ret);
	return ret;
}

static void __clk_pll_div_set(struct clk_pll_div *clkpd, uint32_t val)
{
	uint32_t m = (BIT(clkpd->div_width) - 1) << clkpd->div_shift;
	uint32_t s = clkpd->div_shift;

	clk_regmap_update(&clkpd->clkp.clkr, clkpd->div_ofs, m, val << s);
}

static uint32_t __clk_pll_div_get(struct clk_pll_div *clkpd)
{
	uint32_t m = (BIT(clkpd->div_width) - 1) << clkpd->div_shift;
	uint32_t s = clkpd->div_shift;

	return (clk_regmap_read(&clkpd->clkp.clkr, clkpd->div_ofs) & m) >> s;
}

static long clk_pll_div_round_rate(struct clk_hw *hw, unsigned long rate,
				   unsigned long *parent_rate)
{
	struct clk_pll_div *clkpd = to_clk_pll_div(hw);
	const struct div_table *dv;

	/* lookup div in dtbl */
	dv = dtbl_find_by_rate(clkpd->div_tbl, rate);
	if (!dv)
		return 0;

	rate *= dv->div;
	rate = clk_pll_round_rate(hw, rate, parent_rate);
	return rate / dv->div;
}

static unsigned long clk_pll_div_recalc_rate(struct clk_hw *hw,
					     unsigned long parent_rate)
{
	struct clk_pll_div *clkpd = to_clk_pll_div(hw);
	unsigned long rate;
	const struct div_table *dv;
	uint32_t val;

	rate = clk_pll_recalc_rate(hw, parent_rate);

	val = __clk_pll_div_get(clkpd);
	dv = dtbl_find_by_val(clkpd->div_tbl, val);
	if (!dv)
		return 0;

	rate /= dv->div;
	pr_debug("%pC: %s: current rate=%lu, div=%d, reg_val=0x%x\n",
		 hw->clk, __func__, rate, dv->div, val);

	return rate;
}

static int clk_pll_div_set_rate(struct clk_hw *hw, unsigned long rate,
				unsigned long parent_rate)
{
	struct clk *clk = hw->clk;
	struct clk_pll_div *clkpd = to_clk_pll_div(hw);
	unsigned long flags;
	const struct div_table *ndv, *cdv;
	unsigned long target;
	uint32_t cur_d;
	int ret;

	/* find next in the dtbl */
	ndv = dtbl_find_by_rate(clkpd->div_tbl, rate);
	if (!ndv)
		return -EINVAL;

	target = rate * ndv->div;

	/* find current in the dtbl */
	cur_d = __clk_pll_div_get(clkpd);
	cdv = dtbl_find_by_val(clkpd->div_tbl, cur_d);
	if (!cdv)
		return -EINVAL;

	pr_debug("%pC: rate=%lu, cdv={%d,0x%x}, ndv={%d,0x%x}\n",
		 clk, rate, cdv->div, cdv->val, ndv->div, ndv->val);

	flags = clk_pll_div_lock(clkpd);

	/* workaround to prevent glitch */
#ifdef CONFIG_COMMON_CLK_RTD129X
	if ((&clkpd->clkp.flags & CLK_PLL_DIV_WORKAROUND) &&
		ndv->val != cdv->val && (ndv->val == 1 || cdv->val == 1)) {

		pr_debug("%pC: apply rate=%u\n", clk, 1000000000);
		clk_pll_set_rate(hw, 1000000000, parent_rate);

		pr_debug("%pC: apply dv={%d, 0x%x}\n", clk, ndv->div, ndv->val);
		__clk_pll_div_set(clkpd, ndv->val);
		cdv = ndv;
	}
#endif

	if (ndv->div > cdv->div)
		__clk_pll_div_set(clkpd, ndv->val);
	ret = clk_pll_set_rate(hw, target, parent_rate);
	if (ndv->div < cdv->div)
		__clk_pll_div_set(clkpd, ndv->val);

	clk_pll_div_unlock(clkpd, flags);

	return ret;
}

static int clk_pll_raw_freq_u64_get(void *data, u64 *val)
{
	struct clk_pll *clkp = to_clk_pll(data);

	*val = __clk_pll_freq_raw_get(clkp);
	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(clk_pll_raw_freq_ops, clk_pll_raw_freq_u64_get, NULL, "%08llx\n");

static int clk_pll_debugfs_init(struct clk_hw *hw, struct dentry *d)
{
	set_clk_rate_debugfs_init(hw, d);
	debugfs_create_file("freq_raw", 0444, d, hw, &clk_pll_raw_freq_ops);
	return 0;
}

const struct clk_ops clk_pll_ops = {
	.debug_init       = clk_pll_debugfs_init,
	.round_rate       = clk_pll_round_rate,
	.recalc_rate      = clk_pll_recalc_rate,
	.set_rate         = clk_pll_set_rate,
	.enable           = clk_pll_enable,
	.disable          = clk_pll_disable,
	.disable_unused   = clk_pll_disable_unused,
	.is_enabled       = clk_pll_is_enabled,
};

const struct clk_ops clk_pll_div_ops = {
	.debug_init       = clk_pll_debugfs_init,
	.round_rate       = clk_pll_div_round_rate,
	.recalc_rate      = clk_pll_div_recalc_rate,
	.set_rate         = clk_pll_div_set_rate,
	.enable           = clk_pll_enable,
	.disable          = clk_pll_disable,
	.disable_unused   = clk_pll_disable_unused,
	.is_enabled       = clk_pll_is_enabled,
};

