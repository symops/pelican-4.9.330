// SPDX-License-Identifier: GPL-2.0-only
 /*
  * Copyright (C) 2019 Realtek Semiconductor Corporation
  * Author: Cheng-Yu Lee <cylee12@realtek.com>
  */
#ifndef __SOC_REALTEK_SB2_H
#define __SOC_REALTEK_SB2_H

/* register definitions */
#define SB2_HD_SEM                      (0x000)
#define SB2_CHIP_ID                     (0x200)
#define SB2_CHIP_INFO                   (0x204)

#define SB2_HD_SEM_NEW_0                (0x620)
#define SB2_HD_SEM_NEW_1                (0x624)
#define SB2_HD_SEM_NEW_2                (0x628)
#define SB2_HD_SEM_NEW_3                (0x62c)
#define SB2_HD_SEM_NEW_4                (0x630)
#define SB2_HD_SEM_NEW_5                (0x634)
#define SB2_HD_SEM_NEW_6                (0x638)
#define SB2_HD_SEM_NEW_7                (0x63c)

#include <linux/arm-smccc.h>
#include <linux/device.h>
#include <linux/regmap.h>
#include <linux/nvmem-consumer.h>
#include <linux/of.h>
#include <linux/of_address.h>

struct sb2_icg_desc;

struct sb2_device {
	struct regmap *regmap;
	const struct sb2_icg_desc *icg_desc;
};

/* common data */
struct sb2_common_data {
	struct device *dev;
	void *base;
	int irq;
};

static inline int sb2_check_perm(struct sb2_common_data *data)
{
	struct device *dev = data->dev;
	struct device_node *np = dev->of_node;
	struct nvmem_cell *cell;
	unsigned char *buf;
	size_t buf_size;
	unsigned char val = 0;

	cell = of_nvmem_cell_get(np, "secure_en");
	if (!IS_ERR(cell)) {
		buf = nvmem_cell_read(cell, &buf_size);
		nvmem_cell_put(cell);
		if (!IS_ERR(buf)) {
			val = buf[0];
			kfree(buf);
		}

		if (val == 0 || val == 1)
			return 0;

		return -EACCES;
	}
	return 0;
}

static inline
void sb2_reg_read(struct sb2_common_data *data, int offset, unsigned int *val)
{
	*val = readl(data->base + offset);
}

static inline
void sb2_reg_write(struct sb2_common_data *data, int offset, unsigned int val)
{
	writel(val, data->base + offset);
}

#endif
