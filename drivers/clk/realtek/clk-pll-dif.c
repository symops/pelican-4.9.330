// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2019 Realtek Semiconductor Corporation
 * Author: Cheng-Yu Lee <cylee12@realtek.com>
 */

#include <linux/clk-provider.h>
#include <linux/clk.h>
#include <linux/spinlock.h>
#include <linux/delay.h>
#include "common.h"
#include "clk-pll.h"

static int clk_pll_dif_enable(struct clk_hw *hw)
{
	struct clk_pll_dif *pll = to_clk_pll_dif(hw);

	pr_debug("%pC: %s\n", hw->clk, __func__);

	clk_regmap_write(&pll->clkr, pll->pll_ofs + 0x0C, pll->adtv_conf[0]);
	clk_regmap_write(&pll->clkr, pll->pll_ofs + 0x08, pll->adtv_conf[1]);
	clk_regmap_write(&pll->clkr, pll->pll_ofs + 0x04, pll->adtv_conf[2]);
	clk_regmap_write(&pll->clkr, pll->pll_ofs + 0x00, pll->adtv_conf[3]);
	udelay(100);

	clk_regmap_write(&pll->clkr, pll->pll_ofs + 0x08, pll->adtv_conf[4]);
	udelay(50);

	clk_regmap_write(&pll->clkr, pll->pll_ofs + 0x08, pll->adtv_conf[5]);
	udelay(200);

	clk_regmap_write(&pll->clkr, pll->pll_ofs + 0x0C, pll->adtv_conf[6]);
	udelay(100);

	clk_regmap_write(&pll->clkr, pll->pll_ofs + 0x04, pll->adtv_conf[7]);

	/* ssc control */
	clk_regmap_write(&pll->clkr, pll->ssc_ofs + 0x00, 0x00000004);
	clk_regmap_write(&pll->clkr, pll->ssc_ofs + 0x04, 0x00006800);
	clk_regmap_write(&pll->clkr, pll->ssc_ofs + 0x0C, 0x00000000);
	clk_regmap_write(&pll->clkr, pll->ssc_ofs + 0x10, 0x00000000);
	clk_regmap_write(&pll->clkr, pll->ssc_ofs + 0x08, 0x001e1f98);
	clk_regmap_write(&pll->clkr, pll->ssc_ofs + 0x00, 0x00000005);
	pll->status = 1;

	return 0;
}

static void clk_pll_dif_disable(struct clk_hw *hw)
{
	struct clk_pll_dif *pll = to_clk_pll_dif(hw);

	pr_debug("%pC: %s\n", hw->clk, __func__);
	clk_regmap_update(&pll->clkr, pll->pll_ofs + 0x04, 0x00080000, 0x0);
	clk_regmap_update(&pll->clkr, pll->pll_ofs + 0x08, 0x00400C03, 0x0);
	clk_regmap_update(&pll->clkr, pll->pll_ofs + 0x0C, 0x00000038, 0x0);

	clk_regmap_write(&pll->clkr, pll->ssc_ofs + 0x00, 0x00000004);
	pll->status = 0;
}

static int clk_pll_dif_is_enabled(struct clk_hw *hw)
{
	struct clk_pll_dif *pll = to_clk_pll_dif(hw);

	return pll->status;
}

static void clk_pll_dif_disable_unused(struct clk_hw *hw)
{
	pr_info("%pC: %s\n", hw->clk, __func__);
	clk_pll_dif_disable(hw);
}

const struct clk_ops clk_pll_dif_ops = {
	.enable           = clk_pll_dif_enable,
	.disable          = clk_pll_dif_disable,
	.disable_unused   = clk_pll_dif_disable_unused,
	.is_enabled       = clk_pll_dif_is_enabled,
};

