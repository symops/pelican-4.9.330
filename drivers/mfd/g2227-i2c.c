// SPDX-License-Identifier: GPL-2.0-only
/*
 * GMT-G2227 PMIC MFD driver
 *
 * Copyright (C) 2016-2019 Realtek Semiconductor Corporation
 * Author: Cheng-Yu Lee <cylee12@realtek.com>
 * Author: Simon Hsu <simon_hsu@realtek.com>
 */

#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/mfd/g2227.h>
#include <linux/mfd/g22xx.h>

static bool g2227_regmap_readable_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case G2227_REG_INTR ... G2227_REG_PWRKEY:
	case G2227_REG_SYS_CONTROL ... G2227_REG_LDO2LDO3_MODE:
	case G2227_REG_DC2_NRMVOLT ... G2227_REG_VERSION:
		return true;
	}
	return false;
}

static bool g2227_regmap_writeable_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case G2227_REG_INTR_MASK ... G2227_REG_PWRKEY:
	case G2227_REG_SYS_CONTROL ... G2227_REG_LDO2LDO3_MODE:
	case G2227_REG_DC2_NRMVOLT ... G2227_REG_LDO2LDO3_SLPVOLT:
		return true;
	}
	return false;
}

static bool g2227_regmap_volatile_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case G2227_REG_INTR_MASK ... G2227_REG_PWRKEY:
	case G2227_REG_SYS_CONTROL:
	case G2227_REG_VERSION:
		return true;
	}
	return false;
}

static const struct regmap_config g2227_regmap_config = {
	.reg_bits         = 8,
	.val_bits         = 8,
	.max_register     = 0x20,
	.cache_type       = REGCACHE_RBTREE,
	.readable_reg     = g2227_regmap_readable_reg,
	.writeable_reg    = g2227_regmap_writeable_reg,
	.volatile_reg     = g2227_regmap_volatile_reg,
};

static int g2227_i2c_probe(struct i2c_client *client,
	const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct g22xx_device *gdev;
	int ret = 0;
	unsigned int rev;

	gdev = devm_kzalloc(dev, sizeof(*gdev), GFP_KERNEL);
	if (gdev == NULL)
		return -ENOMEM;

	gdev->regmap = devm_regmap_init_i2c(client, &g2227_regmap_config);
	if (IS_ERR(gdev->regmap)) {
		ret = PTR_ERR(gdev->regmap);
		dev_err(dev, "failed to allocate regmap: %d\n", ret);
		return ret;
	}

	/* workaround */
	regmap_read(gdev->regmap, G2227_REG_VERSION, &rev);

	/* show version */
	ret = regmap_read(gdev->regmap, G2227_REG_VERSION, &rev);
	if (ret) {
		dev_err(dev, "failed to read version: %d\n", ret);
		return ret;
	}
	dev_info(dev, "g2227 rev%d\n", rev);
	gdev->chip_id = G22XX_DEVICE_ID_G2227;
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

static int g2227_i2c_remove(struct i2c_client *client)
{
	struct g22xx_device *gdev = i2c_get_clientdata(client);

	g22xx_device_exit(gdev);
	return 0;
}

static const struct of_device_id g2227_of_match[] = {
	{ .compatible = "gmt,g2227", },
	{}
};
MODULE_DEVICE_TABLE(of, g2227_of_match);

static const struct i2c_device_id g2227_i2c_id[] = {
	{ "g2227", 0 },
	{}
};
MODULE_DEVICE_TABLE(i2c, g2227_i2c_id);

static struct i2c_driver g2227_i2c_driver = {
	.driver = {
		.name = "g2227",
		.of_match_table = of_match_ptr(g2227_of_match),
	},
	.probe = g2227_i2c_probe,
	.remove = g2227_i2c_remove,
	.id_table = g2227_i2c_id,
};
module_i2c_driver(g2227_i2c_driver);

MODULE_DESCRIPTION("GMT G2227 PMIC MFD Driver");
MODULE_AUTHOR("Simon Hsu <simon_hsu@realtek.com>");
MODULE_AUTHOR("Cheng-Yu Lee <cylee12@realtek.com>");
MODULE_LICENSE("GPL v2");
