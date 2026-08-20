// SPDX-License-Identifier: GPL-2.0
/*
 * selector.c - i2c regulator selector
 *
 * Copyright (C) 2018,2019 Realtek Semiconductor Corporation
 * Author: Cheng-Yu Lee <cylee12@realtek.com>
 */

#define pr_fmt(fmt) "i2c-selector: " fmt

#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/suspend.h>
#include "internal.h"

struct selection_data {
	struct i2c_driver *drv;
	const char *name;
	struct device_node *np;
	u32 reg;
	u32 val;
};

static int selector_read_reg(struct i2c_client *i2c, u8 reg, u8 *val)
{
	struct i2c_msg xfer[2];
	int ret = 0;

	xfer[0].addr = i2c->addr;
	xfer[0].flags = 0;
	xfer[0].len = 1;
	xfer[0].buf = &reg;

	xfer[1].addr = i2c->addr;
	xfer[1].flags = I2C_M_RD;
	xfer[1].len = 1;
	xfer[1].buf = val;

	ret = i2c_transfer(i2c->adapter, xfer, 2);
	if (ret == 2)
		return 0;
	else if (ret < 0)
		return ret;
	else
		return -EIO;
}

static int selector_match_data(struct i2c_client *client,
			       struct selection_data *data, int num)
{
	struct selection_data *d;
	u8 val;
	int i;
	int ret;

	for (i = 0; i < num - 1; i++) {
		d = &data[i];
		ret = selector_read_reg(client, d->reg, &val);
		if (ret)
			return ret;

		if (val == d->val)
			return i;
	}
	return i;
}

const struct i2c_device_id *
selector_match_i2c_device_id(struct device_node *np, struct i2c_driver *drv)
{
	char modalias[I2C_NAME_SIZE];
	const struct i2c_device_id *id = drv->id_table;
	int ret;

	while (id->name[0]) {
		ret = of_modalias_node(np, modalias, sizeof(modalias));
		if (ret)
			continue;
		if (!strcmp(id->name, modalias))
			return id;
		id++;
	}
	return NULL;
}

static void selector_update_regulator_dev(struct device *dev,
					  struct device_node *np, int idx)
{
	struct device_node *saved_np = dev->of_node;
	struct device_node *child;
	struct regulator *reg;
	char name[20];

	for_each_child_of_node(np, child) {
		if (!of_device_is_compatible(child, "supply-selector"))
			continue;

		sprintf(name, "sel-%d", idx);

		dev->of_node = child;
		reg = regulator_get(dev, name);
		if (IS_ERR(reg)) {
			dev_err(dev, "failed to get regulator with %s: %ld\n",
				name, PTR_ERR(reg));
			continue;
		}

		mutex_lock(&reg->rdev->mutex);
		reg->rdev->dev.of_node = child;
		mutex_unlock(&reg->rdev->mutex);

		regulator_put(reg);
	}

	dev->of_node = saved_np;
}

struct drv_match_data {
	const char *name;
	struct i2c_driver *drv;
};

static int drv_match(struct device_driver *drv, void *p)
{
	struct drv_match_data *d = p;

	if (!strcmp(d->name, drv->name))
		d->drv = to_i2c_driver(drv);
	return 0;
}

static struct i2c_driver *
selector_find_i2c_driver_by_name(const char *name)
{
	struct drv_match_data d;
	int ret;

	d.name = name;
	d.drv = ERR_PTR(-EPROBE_DEFER);
	ret = bus_for_each_drv(&i2c_bus_type, NULL, &d, drv_match);
	return ret ? ERR_PTR(ret) : d.drv;
}

static int selector_of_parse_data(struct device_node *np,
				  struct selection_data *data, int num)
{
	struct selection_data *d;
	int i;
	int ret;

	for (i = 0; i < num; i++) {
		d = &data[i];

		ret = of_property_read_string_index(np, "sel-drv-names", i,
						    &d->name);
		if (ret)
			return ret;

		d->drv = selector_find_i2c_driver_by_name(d->name);
		if (IS_ERR(d->drv))
			return PTR_ERR(d->drv);

		d->np = of_parse_phandle(np, "sel-dev-nodes", i);
		if (!d->np)
			return -EINVAL;

		if (i == num - 1)
			break;

		ret = of_property_read_u32_index(np, "sel-matches", i * 2 + 0,
						 &d->reg);
		if (ret)
			return ret;
		ret = of_property_read_u32_index(np, "sel-matches", i * 2 + 1,
						 &d->val);
		if (ret)
			return ret;
	}
	return 0;
}

static int selector_probe(struct i2c_client *client,
			  const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct device_node *np = dev->of_node;
	const struct i2c_device_id *new_id;
	struct selection_data *data;
	struct selection_data *d;
	int ret;
	int num;
	int idx;

	num = of_property_count_strings(np, "sel-drv-names");
	if (num < 0)
		return num;

	data = devm_kcalloc(dev, num, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	ret = selector_of_parse_data(np, data, num);
	if (ret) {
		if (ret == -EPROBE_DEFER) {
			dev_dbg(dev, "i2c driver is not ready\n");
			return ret;
		}

		dev_err(dev, "failed to parse of data: %d\n", ret);
		return ret;
	}

	idx = selector_match_data(client, data, num);
	if (idx < 0) {
		dev_err(dev, "failed to match data: %d\n", idx);
		return idx;
	}
	d = &data[idx];

	dev_info(dev, "select idx=%d, driver=%s, np=%s\n", idx,
		 d->drv->driver.name,  d->np->name);
	dev->driver = &d->drv->driver;
	dev->of_node = d->np;

	new_id = selector_match_i2c_device_id(d->np, d->drv);
	if (!new_id) {
		dev_err(dev, "no match i2c_device_id\n");
		return -EINVAL;
	}

	/* probe */
	ret = d->drv->probe(client, new_id);
	if (ret)
		return ret;

	devm_kfree(dev, data);

	selector_update_regulator_dev(dev, np, idx);
	return 0;
}

static const struct i2c_device_id selector_ids[] = {
	{ "regulator-selector", 0 },
	{}
};
MODULE_DEVICE_TABLE(i2c, selector_ids);

static struct i2c_driver selector_driver = {
	.driver = {
		.name = "i2c-sel",
		.owner = THIS_MODULE,
	},
	.id_table = selector_ids,
	.probe    = selector_probe,
};
module_i2c_driver(selector_driver);

MODULE_DESCRIPTION("I2C Selector Driver");
MODULE_AUTHOR("Cheng-Yu Lee <cylee12@realtek.com>");
MODULE_LICENSE("GPL");
