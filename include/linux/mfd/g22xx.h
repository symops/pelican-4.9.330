/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * GMT-G22XX series PMIC MFD
 *
 * Copyright (C) 2019 Realtek Semiconductor Corporation
 * Author: Cheng-Yu Lee <cylee12@realtek.com>
 */

#ifndef __LINUX_MFD_G22XX_H
#define __LINUX_MFD_G22XX_H

#include <linux/regmap.h>

struct g22xx_device {
	u32 chip_id;
	u32 chip_rev;
	struct device *dev;
	struct regmap *regmap;
};

int g22xx_device_init(struct g22xx_device *gdev);
void g22xx_device_exit(struct g22xx_device *gdev);

#define G22XX_DEVICE_ID_G2227           (2227)
#define G22XX_DEVICE_ID_G2237           (2237)

#endif
