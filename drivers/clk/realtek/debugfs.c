// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2019 Realtek Semiconductor Corporation
 * Author: Cheng-Yu Lee <cylee12@realtek.com>
 */

#include <linux/clk-provider.h>
#include <linux/clk.h>
#include <linux/debugfs.h>
#include "common.h"

static int set_clk_rate_u64_set(void *data, u64 val)
{
	struct clk_hw *hw = data;

	return clk_set_rate(hw->clk, (unsigned long)(val));
}

DEFINE_SIMPLE_ATTRIBUTE(set_clk_rate_ops, NULL,
			set_clk_rate_u64_set, "%llu\n");

int set_clk_rate_debugfs_init(struct clk_hw *hw, struct dentry *d)
{
	debugfs_create_file("set_clk_rate", 0644, d, hw, &set_clk_rate_ops);
	return 0;
}
