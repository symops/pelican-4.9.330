// SPDX-License-Identifier: GPL-2.0-only
/*
 * GMT-G2237 PMIC MFD driver
 *
 * Copyright (C) 2017-2019 Realtek Semiconductor Corporation
 * Author: Cheng-Yu Lee <cylee12@realtek.com>
 */

#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/mfd/g22xx.h>
#include <linux/mfd/g2237.h>
#include <linux/regmap.h>
#include <linux/slab.h>

static bool g2237_regmap_readable_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case G2237_REG_INTR ... G2237_REG_PWRKEY:
	case G2237_REG_SYS_CONTROL ... G2237_REG_VERSION:
		return true;
	}
	return false;
}

static bool g2237_regmap_writeable_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case G2237_REG_INTR_MASK ... G2237_REG_PWRKEY:
	case G2237_REG_SYS_CONTROL ... G2237_REG_LDO1_SLPVOLT:
		return true;
	}
	return false;
}

static bool g2237_regmap_volatile_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case G2237_REG_INTR ... G2237_REG_PWRKEY:
	case G2237_REG_SYS_CONTROL:
	case G2237_REG_CHIP_ID:
	case G2237_REG_VERSION:
		return true;
	}
	return false;
}

static const struct regmap_config g2237_regmap_config = {
	.reg_bits         = 8,
	.val_bits         = 8,
	.max_register     = 0x15,
	.cache_type       = REGCACHE_RBTREE,
	.readable_reg     = g2237_regmap_readable_reg,
	.writeable_reg    = g2237_regmap_writeable_reg,
	.volatile_reg     = g2237_regmap_volatile_reg,
};

static int g2237_i2c_probe(struct i2c_client *client,
			   const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct g22xx_device *gdev;
	int ret = 0;
	unsigned int chip_id, rev;

	gdev = devm_kzalloc(dev, sizeof(*gdev), GFP_KERNEL);
	if (gdev == NULL)
		return -ENOMEM;

	gdev->regmap = devm_regmap_init_i2c(client, &g2237_regmap_config);
	if (IS_ERR(gdev->regmap)) {
		ret = PTR_ERR(gdev->regmap);
		dev_err(dev, "failed to allocate regmap: %d\n", ret);
		return ret;
	}

	ret = regmap_read(gdev->regmap, G2237_REG_CHIP_ID, &chip_id);
	if (ret) {
		dev_err(dev, "failed to read chip_id: %d\n", ret);
		return ret;
	}
	if (chip_id != 0x25) {
		dev_err(dev, "invaild chip_id(%02x)\n", chip_id);
		return -EINVAL;
	}
	regmap_read(gdev->regmap, G2237_REG_VERSION, &rev);

	dev_info(dev, "g2237 rev%d\n", rev);
	gdev->chip_id = G22XX_DEVICE_ID_G2237;
	gdev->chip_rev = rev;
	gdev->dev = dev;

	i2c_set_clientdata(client, gdev);
	ret = g22xx_device_init(gdev);
	if (ret) {
		dev_err(dev, "failed to add sub-devices: %d\n", ret);
		return ret;
	}
	return ret;
}

static int g2237_i2c_remove(struct i2c_client *client)
{
	struct g22xx_device *gdev = i2c_get_clientdata(client);

	g22xx_device_exit(gdev);
	return 0;
}

static const struct of_device_id g2237_of_match[] = {
	{ .compatible = "gmt,g2237", },
	{}
};
MODULE_DEVICE_TABLE(of, g2237_of_match);

static const struct i2c_device_id g2237_i2c_id[] = {
	{ "g2237", 0 },
	{}
};
MODULE_DEVICE_TABLE(i2c, g2237_i2c_id);

static struct i2c_driver g2237_i2c_driver = {
	.driver = {
		.name = "g2237",
		.of_match_table = of_match_ptr(g2237_of_match),
	},
	.probe = g2237_i2c_probe,
	.remove = g2237_i2c_remove,
	.id_table = g2237_i2c_id,
};
module_i2c_driver(g2237_i2c_driver);

MODULE_DESCRIPTION("GMT G237 PMIC MFD Driver");
MODULE_AUTHOR("Cheng-Yu Lee <cylee12@realtek.com>");
MODULE_LICENSE("GPL v2");

