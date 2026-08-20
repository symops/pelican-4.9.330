// SPDX-License-Identifier: GPL-2.0-only
/*
 * GMT-APW888X series PMIC MFD core
 *
 * Copyright (C) 2019 Realtek Semiconductor Corporation
 * Author: Cheng-Yu Lee <cylee12@realtek.com>
 */

#include <linux/regmap.h>
#include <linux/mfd/core.h>
#include <linux/mfd/apw888x.h>

static struct mfd_cell apw8889_devs[] = {
	{
		.name = "apw8889-regulator",
		.of_compatible = "anpec,apw8889-regulator",
	},
};

static struct mfd_cell apw8886_devs[] = {
	{
		.name = "apw8886-regulator",
		.of_compatible = "anpec,apw8886-regulator",
	},
};

int apw888x_device_init(struct apw888x_device *adev)
{
	switch (adev->chip_id) {
	case APW888X_DEVICE_ID_APW8889:
		return devm_mfd_add_devices(adev->dev, PLATFORM_DEVID_NONE,
			apw8889_devs, ARRAY_SIZE(apw8889_devs), 0, 0, 0);
	case APW888X_DEVICE_ID_APW8886:
	case APW888X_DEVICE_ID_APW7899:
		return devm_mfd_add_devices(adev->dev, PLATFORM_DEVID_NONE,
			apw8886_devs, ARRAY_SIZE(apw8886_devs), 0, 0, 0);
	default:
		return -EINVAL;
	}
	return 0;
}

void apw888x_device_exit(struct apw888x_device *adev)
{}
