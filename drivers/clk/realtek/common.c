// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2019 Realtek Semiconductor Corporation
 * Author: Cheng-Yu Lee <cylee12@realtek.com>
 */

#include <linux/clk.h>
#include <linux/clkdev.h>
#include <linux/clk-provider.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/pm.h>
#include "common.h"
#include "clk-pll.h"
#include "clk-regmap-gate.h"
#include "clk-regmap-mux.h"
#include <linux/mfd/syscon.h>
#include <soc/realtek/rtk_sb2_sem.h>

struct rtk_clk_data *rtk_clk_alloc_data(int clk_num)
{
	struct rtk_clk_data *data;

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data)
		return NULL;

	data->clk_num = clk_num;
	data->clk_data.clk_num = clk_num;
	data->clk_data.clks = kcalloc(clk_num, sizeof(*data->clk_data.clks),
				      GFP_KERNEL);
	if (!data->clk_data.clks)
		goto free_data;
	return data;

free_data:
	kfree(data->clk_data.clks);
	kfree(data);
	return NULL;
}

void rtk_clk_free_data(struct rtk_clk_data *data)
{
	kfree(data->clk_data.clks);
	kfree(data);
}

int rtk_clk_of_init_data(struct device_node *np, struct rtk_clk_data *data)
{
	struct regmap *regmap;
	struct sb2_sem *lock;
	int ret;

	regmap = syscon_node_to_regmap(of_get_parent(np));
	if (IS_ERR(regmap)) {
		ret = PTR_ERR(regmap);
		pr_err("%s: failed to get regmap form %s: %d\n", __func__,
			np->name, ret);
		return ret;
	}
	data->regmap = regmap;

	lock = of_sb2_sem_get(np, 0);
	if (!IS_ERR(lock)) {
		data->lock = lock;
		pr_info("%s: %s: lock used\n", __func__, np->name);
	}

	return 0;
}

static inline
int __cell_clk_add(struct clk_onecell_data *clk_data, int i, struct clk *clk)
{
	if (clk_data->clks[i]) {
		pr_err("%s: failed to add %pC, cell%d is used by %pC\n",
			__func__, clk, i, clk_data->clks[i]);
		return -EINVAL;
	}
	clk_data->clks[i] = clk;
	return 0;
}

static void rtk_clk_init_clk_regmap(struct clk_regmap *clkr,
				    struct rtk_clk_data *data,
				    int shared)
{
	clkr->regmap = data->regmap;

	if (!shared)
		return;

	clkr->lock = data->lock;
	clkr->shared = shared;
	WARN_ON_ONCE(!clkr->lock);
}

#define CLK_TYPE_DEFAULT                (0x0)
#define CLK_TYPE_REGMAP                 (0x8)
#define CLK_TYPE_REGMAP_PLL             (0x1 | CLK_TYPE_REGMAP)
#define CLK_TYPE_REGMAP_MUX             (0x2 | CLK_TYPE_REGMAP)
#define CLK_TYPE_REGMAP_GATE            (0x3 | CLK_TYPE_REGMAP)

static inline int __hw_to_type(struct clk_hw *hw)
{
	const struct clk_ops *ops = hw->init->ops;

	if (ops == &clk_pll_ops || ops == &clk_pll_div_ops)
		return CLK_TYPE_REGMAP_PLL;
	if (ops == &clk_regmap_mux_ops)
		return CLK_TYPE_REGMAP_MUX;
	if (ops == &clk_regmap_gate_ops)
		return CLK_TYPE_REGMAP_GATE;
#ifdef CONFIG_CLK_PLL_DIF
	/*
	 * clk_pll_dif is not based struct clk_pll, so
	 * return as CLK_TYPE_REGMAP to setup internal
	 * clk_reg
	 */
	if (ops == &clk_pll_dif_ops)
		return CLK_TYPE_REGMAP;
#endif
#ifdef CONFIG_CLK_PLL_PSAUD
	/*
	 * clk_pll_psaud is not based struct clk_pll, so
	 * return as CLK_TYPE_REGMAP to setup internal
	 * clk_reg
	 */
	if (ops == &clk_pll_psaud_ops)
		return CLK_TYPE_REGMAP;
#endif
#ifdef CONFIG_CLK_PLL_EDP
	/*
	 * clk_pll_edp is not based struct clk_pll, so
	 * return as CLK_TYPE_REGMAP to setup internal
	 * clk_reg
	 */
	if (ops == &clk_pll_edp_ops)
		return CLK_TYPE_REGMAP;
#endif
#ifdef CONFIG_CLK_PLL_PIXEL
	/*
	 * clk_pll_pixel is not based struct clk_pll, so
	 * return as CLK_TYPE_REGMAP to setup internal
	 * clk_reg
	 */
	if (ops == &clk_pll_pixel_ops)
		return CLK_TYPE_REGMAP;
#endif

	return CLK_TYPE_DEFAULT;
}

static
struct clk *rtk_clk_register_hw(struct device *dev, struct rtk_clk_data *data,
				struct clk_hw *hw)
{
	int type;

	type = __hw_to_type(hw);
	if (type & CLK_TYPE_REGMAP) {
		struct clk_regmap *clkr = to_clk_regmap(hw);

		rtk_clk_init_clk_regmap(clkr, data, clkr->shared);
	}

	return clk_register(dev, hw);
}

int rtk_clk_add_hws_from(struct device *dev, struct rtk_clk_data *data,
			 struct clk_hw **hws, int size, int start_index)
{
	struct clk_onecell_data *clk_data = &data->clk_data;
	int i, j;

	for (i = 0, j = start_index; i < size; i++, j++) {
		struct clk_hw *hw = hws[i];
		const char *name;
		struct clk *clk;

		if (IS_ERR_OR_NULL(hw))
			continue;

		name = hw->init->name;
		dev_dbg(dev, "%s: registering clk_hw '%s' (idx=%d) at slot%d\n",
			__func__, name, i, j);

		clk = rtk_clk_register_hw(dev, data, hw);
		if (IS_ERR(clk)) {
			pr_err("%s: failed to register clk_hw '%s' (idx=%d): %ld\n",
			       __func__, name, i, PTR_ERR(clk));
			continue;
		}

		clk_register_clkdev(clk, name, NULL);
		__cell_clk_add(clk_data, j, clk);
	}
	return 0;
}

static
struct clk *rtk_clk_register_composite(struct device *dev,
				       struct rtk_clk_data *data,
				       struct clk_composite_data *comp)
{
	struct clk_regmap_mux *clkm = NULL;
	const struct clk_ops *mux_op = NULL;
	struct clk_hw *mux_hw = NULL;
	struct clk_regmap_gate *clkg = NULL;
	const struct clk_ops *gate_op = NULL;
	struct clk_hw *gate_hw = NULL;
	struct clk *clk;

	if (comp->mux_ofs != CLK_OFS_INVALID) {
		clkm = kzalloc(sizeof(*clkm), GFP_KERNEL);
		if (!clkm) {
			clk = ERR_PTR(-ENOMEM);
			goto check_err;
		}

		clkm->mux_ofs     = comp->mux_ofs;
		clkm->mask        = BIT(comp->mux_width) - 1;
		clkm->shift       = comp->mux_shift;

		rtk_clk_init_clk_regmap(&clkm->clkr, data, comp->shared);

		mux_op = &clk_regmap_mux_ops;
		mux_hw = &__clk_regmap_mux_hw(clkm);
	}

	if (comp->gate_ofs != CLK_OFS_INVALID) {
		clkg = kzalloc(sizeof(*clkg), GFP_KERNEL);
		if (!clkg) {
			clk = ERR_PTR(-ENOMEM);
			goto check_err;
		}

		clkg->gate_ofs    = comp->gate_ofs;
		clkg->bit_idx     = comp->gate_shift;
		clkg->write_en    = comp->gate_write_en;

		rtk_clk_init_clk_regmap(&clkg->clkr, data, comp->shared);

		gate_op = &clk_regmap_gate_ops;
		gate_hw = &__clk_regmap_gate_hw(clkg);
	}

	clk = clk_register_composite(NULL, comp->name, comp->parent_names,
				     comp->num_parents, mux_hw, mux_op,
				     NULL, NULL, gate_hw, gate_op, comp->flags);
check_err:
	if (IS_ERR(clk)) {
		kfree(clkm);
		kfree(clkg);
	}
	return clk;
}

int rtk_clk_add_composites(struct device *dev, struct rtk_clk_data *data,
			   struct clk_composite_data *comps, int num)
{
	struct clk_onecell_data *clk_data = &data->clk_data;
	int i;

	for (i = 0; i < num; i++) {
		struct clk_composite_data *comp = &comps[i];
		const char *name = comp->name;
		struct clk *clk;

		clk = rtk_clk_register_composite(dev, data, comp);
		if (IS_ERR(clk)) {
			pr_err("%s: failed to add composite%d(%s): %ld\n",
				__func__, i, name, PTR_ERR(clk));
			continue;
		}

		clk_register_clkdev(clk, name, NULL);
		__cell_clk_add(clk_data, comp->id, clk);
	}

	return 0;
}

static
struct clk *rtk_clk_register_gate(struct device *dev, struct rtk_clk_data *data,
				  struct clk_gate_data *gate)
{
	struct clk_regmap_gate *clkg;
	struct clk_init_data init = { 0 };
	struct clk_hw *hw;

	clkg = kzalloc(sizeof(*clkg), GFP_KERNEL);
	if (!clkg)
		return ERR_PTR(-ENOMEM);

	clkg->gate_ofs    = gate->gate_ofs;
	clkg->bit_idx     = gate->gate_shift;
	clkg->write_en    = gate->gate_write_en;

	rtk_clk_init_clk_regmap(&clkg->clkr, data, gate->shared);

	init.name         = gate->name;
	init.ops          = &clk_regmap_gate_ops;
	init.flags         = gate->flags;
	if (gate->parent) {
		init.parent_names = &gate->parent;
		init.num_parents  = 1;
	}

	hw = &__clk_regmap_gate_hw(clkg);
	hw->init = &init;
	return clk_register(dev, hw);
}

int rtk_clk_add_gates(struct device *dev, struct rtk_clk_data *data,
		      struct clk_gate_data *gates, int num)
{
	struct clk_onecell_data *clk_data = &data->clk_data;
	int i;

	for (i = 0; i < num; i++) {
		struct clk_gate_data *gate = &gates[i];
		const char *name = gate->name;
		struct clk *clk;

		clk = rtk_clk_register_gate(dev, data, gate);
		if (IS_ERR(clk)) {
			pr_err("%s: failed to add gate%d(%s): %ld\n", __func__,
				i, name, PTR_ERR(clk));
			continue;
		}

		clk_register_clkdev(clk, name, NULL);
		__cell_clk_add(clk_data, gate->id, clk);
	}

	return 0;
}

