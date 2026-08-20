// SPDX-License-Identifier: GPL-2.0-only
/*
 * GMT-G22XX series PMIC MFD core
 *
 * Copyright (C) 2019 Realtek Semiconductor Corporation
 * Author: Cheng-Yu Lee <cylee12@realtek.com>
 */

#include <linux/regmap.h>
#include <linux/mfd/core.h>
#include <linux/mfd/g22xx.h>

static struct mfd_cell g2227_devs[] = {
	{
		.name = "g2227-regulator",
		.of_compatible = "gmt,g2227-regulator",
	},
};

static struct mfd_cell g2237_devs[] = {
	{
		.name = "g2237-regulator",
		.of_compatible = "gmt,g2237-regulator",
	},
};

int g22xx_device_init(struct g22xx_device *gdev)
{
	switch (gdev->chip_id) {
	case G22XX_DEVICE_ID_G2227:
		return devm_mfd_add_devices(gdev->dev, PLATFORM_DEVID_NONE,
			g2227_devs, ARRAY_SIZE(g2227_devs), 0, 0, 0);
	case G22XX_DEVICE_ID_G2237:
		return devm_mfd_add_devices(gdev->dev, PLATFORM_DEVID_NONE,
			g2237_devs, ARRAY_SIZE(g2237_devs), 0, 0, 0);
	default:
		return -EINVAL;
	}
	return 0;
}

void g22xx_device_exit(struct g22xx_device *gdev)
{}
