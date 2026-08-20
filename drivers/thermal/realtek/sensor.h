// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2018 Realtek Semiconductor Corporation
 * Author: Cheng-Yu Lee <cylee12@realtek.com>
 */

#ifndef __REALTEK_THERMAL_H
#define __REALTEK_THERMAL_H

#include <linux/device.h>
#include <linux/list.h>
#include <linux/thermal.h>
#include <linux/notifier.h>
#include <linux/ktime.h>
#include <linux/io.h>
#include <linux/regmap.h>

struct thermal_sensor_device;
struct thermal_cooling_device;

struct thermal_sensor_hw_ops {
	int (*init)(struct thermal_sensor_device *);
	void (*exit)(struct thermal_sensor_device *);
	void (*reset)(struct thermal_sensor_device *);
	int (*get_temp)(struct thermal_sensor_device *, int *);
};

struct thermal_sensor_desc {
	const struct thermal_sensor_hw_ops *hw_ops;
	int reset_time_ms;
	int status_ofs;
	int status_cnt;
};

struct thermal_sensor_device {
	struct device *dev;
	const struct thermal_zone_of_device_ops *ops;
	struct thermal_zone_device *tz;
	void *base;
	struct regmap *regmap;

	const struct thermal_sensor_desc *desc;
	struct thermal_sensor *sensors;

	void *priv_data;
	ktime_t last_reset_time;

#ifdef CONFIG_RTK_THERMAL_EOH
	unsigned int eoh_state;
#define THERMAL_SENSOR_EOH_NONE    0
#define THERMAL_SENSOR_EOH_START   1
#define THERMAL_SENSOR_EOH_POLL    2
	unsigned int eoh_freq;
	unsigned int eoh_restore_ms;
	ktime_t eoh_time;
	struct notifier_block eoh_cpufreq_nb;
#endif /* CONFIG_RTK_THERMAL_EOH */
};

static inline
int thermal_sensor_device_reg_read(struct thermal_sensor_device *tdev,
		int offset, unsigned int *val)
{
	*val = readl(tdev->base + offset);
	return 0;
}

static inline
int thermal_sensor_device_reg_write(struct thermal_sensor_device *tdev,
		int offset, unsigned int val)
{
	writel(val, tdev->base + offset);
	return 0;
}


extern const struct thermal_sensor_desc rtd119x_sensor_desc;
extern const struct thermal_sensor_desc rtd129x_sensor_desc;
extern const struct thermal_sensor_desc rtd139x_sensor_desc;
extern const struct thermal_sensor_desc rtd161x_sensor_desc;
extern const struct thermal_sensor_desc rtd131x_sensor_desc;

/* helper function */
static inline int __signext(int signb, unsigned int v)
{
	int x = 31 - signb;

	return ((int)(v << x)) >> x;
}

static inline int is_vaild_temp(int temp)
{
	if (temp < -3000 || temp > 150000)
		return 0;
	return 1;
}

static inline bool cdev_is_cpufreq(struct thermal_cooling_device *cdev)
{
	return !strncmp("thermal-cpufreq-0", cdev->type, THERMAL_NAME_LENGTH);
}


#ifdef CONFIG_RTK_THERMAL_EOH
void thermal_sensor_eoh_handle_restore(struct thermal_sensor_device *tdev);
void thermal_sensor_eoh_handle_overheat(struct thermal_sensor_device *tdev,
					int i, enum thermal_trend trend);
int thermal_sensor_add_eoh(struct thermal_sensor_device *tdev);
void thermal_sensor_remove_eoh(struct thermal_sensor_device *tdev);
#else

static inline
void thermal_sensor_eoh_handle_restore(struct thermal_sensor_device *tdev)
{
}

static inline
void thermal_sensor_eoh_handle_overheat(struct thermal_sensor_device *tdev,
					int i, enum thermal_trend trend)
{
}

static inline
int thermal_sensor_add_eoh(struct thermal_sensor_device *tdev)
{
	return 0;
}

static inline
void thermal_sensor_remove_eoh(struct thermal_sensor_device *tdev)
{
}

#endif /* CONFIG_RTK_THERMAL_EOH */

#endif
