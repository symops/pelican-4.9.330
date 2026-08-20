// SPDX-License-Identifier: GPL-2.0
/*
 *  Copyright (C) 2020 Realtek Semiconductor Corporation
 *  Author: Cheng-Yu Lee <cylee12@realtek.com>
 */

#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/consumer.h>
#include "internal.h"

static struct regulator_dev *dev_to_rdev(struct device *dev)
{
	return container_of(dev, struct regulator_dev, dev);
}

static const char *rdev_get_name(struct regulator_dev *rdev)
{
	if (rdev->constraints && rdev->constraints->name)
		return rdev->constraints->name;
	else if (rdev->desc->name)
		return rdev->desc->name;
	else
		return "";
}

static int match_device_parent(struct device *dev, void *data)
{
	return dev->parent == data;
}

static struct platform_device *
virt_helper_create_platform_device(struct device *dev)
{
	struct platform_device *pdev;
	struct regulator_dev *rdev = dev_to_rdev(dev);
	const char *name = rdev_get_name(rdev);
	int ret;
	char devname[40];

	snprintf(devname, sizeof(devname), "%s-virt", dev_name(dev));

	pdev = platform_device_alloc(devname, PLATFORM_DEVID_NONE);
	if (!pdev)
		return ERR_PTR(-ENOMEM);

	ret = platform_device_add_data(pdev, name, strlen(name) + 1);
	if (ret)
		goto fail;

	pdev->dev.parent = dev;
	ret = platform_device_add(pdev);
	if (ret)
		goto fail;

	/* add driver_override after platform device added to prevent probe
	 * immediately.
	 */
	pdev->driver_override = kstrdup("reg-virt-consumer", GFP_KERNEL);
	if (!pdev->driver_override)
		dev_warn(dev, "driver_override is not set\n");

	return pdev;

fail:
	platform_device_put(pdev);
	return ERR_PTR(ret);
}


static int virt_helper_add_consumer(struct device *dev,
				    struct class_interface *class_intf)
{
	struct platform_device *pdev;
	struct regulator_dev *rdev = dev_to_rdev(dev);
	const char *name = rdev_get_name(rdev);
	int ret;

	dev_info(dev, "add virtual consumer for '%s'\n", name);

	pdev = virt_helper_create_platform_device(dev);
	if (IS_ERR(pdev)) {
		ret = PTR_ERR(pdev);
		dev_err(dev, "failed to create platform device: %d\n", ret);
		return ret;
	}
	return 0;
}

static void virt_helper_remove_consumer(struct device *dev,
					struct class_interface *class_intf)
{
	struct device *res;
	struct platform_device *pdev;

	res = bus_find_device(&platform_bus_type, NULL, dev,
			      match_device_parent);
	if (!res)
		return;

	pdev = to_platform_device(res);
	platform_device_unregister(pdev);
}

static struct class_interface virt_helper_interface = {
	.add_dev    = virt_helper_add_consumer,
	.remove_dev = virt_helper_remove_consumer,
};

static struct class *virt_helper_get_regulator_class(void)
{
	struct regulator *regulator;
	struct class *cls;

	regulator = regulator_get(NULL, "regulator-dummy");

	if (IS_ERR_OR_NULL(regulator))
		return NULL;
	cls = regulator->rdev->dev.class;
	regulator_put(regulator);
	return cls;
}

static int __init virt_helper_init(void)
{
	struct class *cls;

	cls = virt_helper_get_regulator_class();
	if (!cls)
		return -EINVAL;

	virt_helper_interface.class = cls;
	return class_interface_register(&virt_helper_interface);
}
module_init(virt_helper_init);

static void  __exit virt_helper_exit(void)
{
	class_interface_unregister(&virt_helper_interface);
}
module_exit(virt_helper_exit);

MODULE_LICENSE("GPL v2");
