// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2017,2019,2020 Realtek Semiconductor Corp.
 */

#include <linux/err.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/rtc.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/of_irq.h>
#include <linux/mfd/syscon.h>
#include <linux/regmap.h>
#include <linux/clk.h>
#include <linux/reset.h>
#include <soc/realtek/rtk_iso.h>

#define MIS_RTCSEC                      0x00
#define MIS_ALMMIN                      0x14
#define MIS_RTCSTOP                     0x24
#define MIS_RTCACR                      0x28
#define MIS_RTCEN                       0x2C
#define MIS_RTCCR                       0x30
#define MIS_RTCACR2                     0x34

struct rtk_rtc_device {
	struct rtc_device *rtc;
	struct device *dev;
	struct regmap *regmap;
	void __iomem *base;
	unsigned long features;
	struct clk *clk;
	struct reset_control *rstc;
	int irq;
	time64_t base_time;
	int bias;
	spinlock_t lock;
};

#define RTK_RTC_HAS_CLK_EN              0x1
#define RTK_RTC_HAS_RSTC                0x2
#define RTK_RTK_HAS_BIAS                0x4

static inline int rtk_rtc_clk_is_required(struct rtk_rtc_device *rdev)
{
	return !!(rdev->features & RTK_RTC_HAS_CLK_EN);
}

static inline int rtk_rtc_rstc_is_required(struct rtk_rtc_device *rdev)
{
	return !!(rdev->features & RTK_RTC_HAS_RSTC);
}

static inline int rtk_rtc_bias_is_supported(struct rtk_rtc_device *rdev)
{
	return !!(rdev->features & RTK_RTK_HAS_BIAS);
}

static inline int rtk_rtc_reg_read(struct rtk_rtc_device *rdev,
				   unsigned int offset, unsigned int *val)
{
	*val = readl(rdev->base + offset);
	return 0;
}

static inline int rtk_rtc_reg_bulk_read(struct rtk_rtc_device *rdev,
					unsigned int offset, unsigned int *val,
					size_t val_count)
{
	int i;

	for (i = 0; i < val_count; i++)
		rtk_rtc_reg_read(rdev, offset + i * 4, val + i);
	return 0;
}

static inline int rtk_rtc_reg_write(struct rtk_rtc_device *rdev,
				    unsigned int offset, unsigned int val)
{
	writel(val, rdev->base + offset);
	return 0;
}

static inline int rtk_rtc_reg_bulk_write(struct rtk_rtc_device *rdev,
					 unsigned int offset, unsigned int *val,
					 size_t val_count)
{
	int i;

	for (i = 0; i < val_count; i++)
		rtk_rtc_reg_write(rdev, offset + i * 4, *(val + i));
	return 0;
}

static void rtk_rtc_enable(struct rtk_rtc_device *rdev, int enabled)
{
	rtk_rtc_reg_write(rdev, MIS_RTCEN, enabled ? 0x5A : 0x00);
}

static void rtk_rtc_clear_ints(struct rtk_rtc_device *rdev)
{
	regmap_write(rdev->regmap, ISO_ISR,
		     ISO_ISR_RTC_ALARM_INT | ISO_ISR_CLEAR_DATA);
}

static inline
int time2reg(struct rtk_rtc_device *rdev, time64_t time, unsigned int *reg)
{
	unsigned int t;

	if (time < rdev->base_time)
		return -EINVAL;

	t = time - rdev->base_time;
	reg[0] = (t % 60) << 1;
	reg[1] = (t / 60) % 60;
	reg[2] = (t / 3600) % 24;
	reg[3] = (t / 86400) & 0xFF;
	reg[4] = (t / 86400) >> 8;
	if (reg[4] > 0x3F)
		return -EOVERFLOW;
	return 0;
}

static inline
time64_t reg2time(struct rtk_rtc_device *rdev, unsigned int *reg)
{
	time64_t time;

	time = (time64_t)reg[4] * (86400 * 256) + reg[3] * 86400 +
		reg[2] * 3600 + reg[1] * 60 + (reg[0] >> 1) + rdev->base_time;
	return time;
}


static void rtk_rtc_check_rtcacr(struct rtk_rtc_device *rdev)
{
	unsigned long flags;
	unsigned int val;
	unsigned int reg[5] = { 0 };

	spin_lock_irqsave(&rdev->lock, flags);

	rtk_rtc_reg_read(rdev, MIS_RTCACR, &val);
	if ((val & 0x80) == 0x80) {
		spin_unlock_irqrestore(&rdev->lock, flags);
		return;
	}

	rtk_rtc_reg_write(rdev, MIS_RTCACR,  0x80);
	rtk_rtc_reg_write(rdev, MIS_RTCCR,   0x40);
	rtk_rtc_reg_write(rdev, MIS_RTCCR,   0x00);
	if (rtk_rtc_bias_is_supported(rdev))
		rtk_rtc_reg_write(rdev, MIS_RTCACR2, rdev->bias);
	rtk_rtc_reg_bulk_write(rdev, MIS_RTCSEC, reg, 5);
	rtk_rtc_enable(rdev, 1);
	spin_unlock_irqrestore(&rdev->lock, flags);
}

static inline
time64_t rtk_rtc_time64_get(struct rtk_rtc_device *rdev)
{
	unsigned int reg[5];
	unsigned long flags;

	spin_lock_irqsave(&rdev->lock, flags);
	rtk_rtc_reg_bulk_read(rdev, MIS_RTCSEC, reg, 5);
	if (reg[0] == 0)
		rtk_rtc_reg_bulk_read(rdev, MIS_RTCSEC, reg, 5);
	spin_unlock_irqrestore(&rdev->lock, flags);

	return reg2time(rdev, reg);
}

static inline
int rtk_rtc_time64_set(struct rtk_rtc_device *rdev, time64_t time)
{
	unsigned long flags;
	unsigned int reg[5];
	int ret;

	ret = time2reg(rdev, time, reg);
	if (ret)
		return ret;

	spin_lock_irqsave(&rdev->lock, flags);
	rtk_rtc_enable(rdev, 0);
	rtk_rtc_reg_bulk_write(rdev, MIS_RTCSEC, reg, 5);
	rtk_rtc_enable(rdev, 1);
	spin_unlock_irqrestore(&rdev->lock, flags);

	return 0;
}

static inline
time64_t rtk_rtc_alarm_time64_get(struct rtk_rtc_device *rdev)
{
	unsigned int reg[5] = { 0 };
	unsigned long flags;

	spin_lock_irqsave(&rdev->lock, flags);
	rtk_rtc_reg_bulk_read(rdev, MIS_ALMMIN, reg + 1, 4);
	spin_unlock_irqrestore(&rdev->lock, flags);

	return reg2time(rdev, reg);
}

static inline
int rtk_rtc_alarm_time64_set(struct rtk_rtc_device *rdev, time64_t time)
{
	unsigned long flags;
	unsigned int reg[5];
	int ret;

	ret = time2reg(rdev, time, reg);
	if (ret)
		return ret;

	spin_lock_irqsave(&rdev->lock, flags);
	rtk_rtc_reg_bulk_write(rdev, MIS_ALMMIN, reg + 1, 4);
	spin_unlock_irqrestore(&rdev->lock, flags);

	return 0;
}

static int rtk_rtc_alarm_enabled(struct rtk_rtc_device *rdev)
{
	int val;

	regmap_read(rdev->regmap, ISO_RTC, &val);
	return val & 0x1;
}

static void rtk_rtc_alarm_enable(struct rtk_rtc_device *rdev, int enabled)
{
	unsigned long flags;

	spin_lock_irqsave(&rdev->lock, flags);
	if (enabled) {
		rtk_rtc_clear_ints(rdev);
		regmap_write(rdev->regmap, ISO_RTC, ISO_RTC_ALARM_INT_EN);
	} else {
		regmap_write(rdev->regmap, ISO_RTC, 0x0);
	}
	spin_unlock_irqrestore(&rdev->lock, flags);
}

static int rtk_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
	struct rtk_rtc_device *rdev = dev_get_drvdata(dev);

	rtc_time64_to_tm(rtk_rtc_time64_get(rdev), tm);

	dev_dbg(dev, "%s: %04d.%02d.%02d %02d:%02d:%02d", __func__,
		1900 + tm->tm_year, tm->tm_mon,	tm->tm_mday, tm->tm_hour,
		tm->tm_min, tm->tm_sec);
	if ((1900 + tm->tm_year) == 2018)
		tm->tm_year = tm->tm_year + 4;
	return rtc_valid_tm(tm);
}

static int rtk_rtc_set_time(struct device *dev, struct rtc_time *tm)
{
	struct rtk_rtc_device *rdev = dev_get_drvdata(dev);

	dev_dbg(dev, "%s: %04d.%02d.%02d %02d:%02d:%02d", __func__,
		1900 + tm->tm_year, tm->tm_mon, tm->tm_mday, tm->tm_hour,
		tm->tm_min, tm->tm_sec);

	return rtk_rtc_time64_set(rdev, rtc_tm_to_time64(tm));
}

static int rtk_rtc_read_alarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	struct rtk_rtc_device *rdev = dev_get_drvdata(dev);

	rtc_time64_to_tm(rtk_rtc_alarm_time64_get(rdev), &alrm->time);
	alrm->enabled = rtk_rtc_alarm_enabled(rdev);
	return 0;
}

static int rtk_rtc_set_alarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	struct rtk_rtc_device *rdev = dev_get_drvdata(dev);
	int ret = 0;
	time64_t now;
	time64_t alarm_time = rtc_tm_to_time64(&alrm->time);

	rtk_rtc_alarm_enable(rdev, 0);

	if (alrm->time.tm_sec)
		alarm_time += 60 - alrm->time.tm_sec;

	now = rtk_rtc_time64_get(rdev);
	if (now > alarm_time)
		return -EINVAL;
	ret = rtk_rtc_alarm_time64_set(rdev, alarm_time);
	if (!ret)
		rtk_rtc_alarm_enable(rdev, alrm->enabled);
	return ret;
}

static int rtk_rtc_alarm_irq_enable(struct device *dev, unsigned int enabled)
{
	struct rtk_rtc_device *rdev = dev_get_drvdata(dev);

	rtk_rtc_alarm_enable(rdev, enabled);
	return 0;
}

static irqreturn_t rtk_rtc_irq_handler(int irq, void *data)
{
	struct platform_device *pdev = data;
	struct rtk_rtc_device *rdev = platform_get_drvdata(pdev);

	rtk_rtc_clear_ints(rdev);

	rtc_update_irq(rdev->rtc, 1, RTC_IRQF | RTC_AF);

	return IRQ_HANDLED;
}

static const struct rtc_class_ops rtk_rtc_alarm_ops = {
	.read_time        = rtk_rtc_read_time,
	.set_time         = rtk_rtc_set_time,
	.read_alarm       = rtk_rtc_read_alarm,
	.set_alarm        = rtk_rtc_set_alarm,
	.alarm_irq_enable = rtk_rtc_alarm_irq_enable,
};

static const struct rtc_class_ops rtk_rtc_ops = {
	.read_time = rtk_rtc_read_time,
	.set_time  = rtk_rtc_set_time,
};

static int of_rtk_rtc_get_config(struct device_node *np,
				 struct rtk_rtc_device *rdev)
{
	unsigned int base_year;

	base_year = 1900;
	of_property_read_u32(np, "rtc-base-year", &base_year);
	rdev->base_time = mktime64(base_year, 1, 1, 0, 0, 0);

	if (!rtk_rtc_bias_is_supported(rdev))
		return 0;

	rdev->bias = 2;
	of_property_read_u32(np, "rtc-bias", &rdev->bias);
	return 0;

}

static int rtk_rtc_probe(struct platform_device *pdev)
{
	struct rtk_rtc_device *rdev;
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	const struct rtc_class_ops *ops = &rtk_rtc_ops;
	int ret;
	struct resource  *res;

	rdev = devm_kzalloc(dev, sizeof(*rdev), GFP_KERNEL);
	if (!rdev)
		return -ENOMEM;

	rdev->features = (unsigned long)of_device_get_match_data(dev);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(dev, "invalid resource\n");
		return -EINVAL;
	}
	rdev->base = devm_ioremap(dev, res->start, resource_size(res));
	if (!rdev->base)
		return -ENOMEM;

	rdev->regmap = syscon_regmap_lookup_by_phandle(np, "realtek,iso");
	if (IS_ERR(rdev->regmap)) {
		ret = PTR_ERR(rdev->regmap);
		dev_err(dev, "failed to get syscon: %d\n", ret);
		return ret;
	}

	if (rtk_rtc_clk_is_required(rdev)) {
		rdev->clk = devm_clk_get(dev, NULL);
		if (IS_ERR(rdev->clk)) {
			ret = PTR_ERR(rdev->clk);
			dev_err(dev, "failed to get clk: %d\n", ret);
			return ret;
		}
	}

	if (rtk_rtc_rstc_is_required(rdev)) {
		rdev->rstc = devm_reset_control_get(dev, NULL);
		if (IS_ERR(rdev->rstc)) {
			ret = PTR_ERR(rdev->rstc);
			dev_err(dev, "failed to get reset control: %d\n", ret);
			return ret;
		}
	}

	of_rtk_rtc_get_config(np, rdev);
	spin_lock_init(&rdev->lock);
	rdev->dev = dev;
	rdev->irq = platform_get_irq(pdev, 0);

	if (rdev->irq > 0) {
		ret = devm_request_threaded_irq(dev, rdev->irq, NULL,
						rtk_rtc_irq_handler,
						IRQF_ONESHOT, dev_name(dev),
						pdev);
		if (ret) {
			dev_err(dev, "failed to request irq%d: %d\n", rdev->irq,
				ret);
			return ret;
		}

		device_init_wakeup(&pdev->dev, true);
		ops = &rtk_rtc_alarm_ops;
	}

	platform_set_drvdata(pdev, rdev);
	if (rdev->rstc)
		reset_control_deassert(rdev->rstc);
	clk_prepare_enable(rdev->clk);

	rtk_rtc_check_rtcacr(rdev);

	rdev->rtc = devm_rtc_device_register(&pdev->dev, "rtk-rtc", ops,
					     THIS_MODULE);
	if (IS_ERR(rdev->rtc)) {
		ret = PTR_ERR(rdev->rtc);
		dev_err(dev, "cannot attach rtc: %d\n", ret);
		goto err_nortc;
	}

	return 0;

err_nortc:
	clk_disable_unprepare(rdev->clk);
	if (rdev->rstc)
		reset_control_assert(rdev->rstc);
	return ret;
}

static int rtk_rtc_remove(struct platform_device *pdev)
{
	struct rtk_rtc_device *rdev = platform_get_drvdata(pdev);

	clk_disable_unprepare(rdev->clk);
	if (rdev->rstc)
		reset_control_assert(rdev->rstc);
	platform_set_drvdata(pdev, NULL);

	return 0;
}

static const struct of_device_id rtk_rtc_ids[] = {
	{
		.compatible = "realtek,rtd1195-rtc",
		.data = (void *)(RTK_RTC_HAS_CLK_EN | RTK_RTC_HAS_RSTC),
	},
	{
		.compatible = "realtek,rtd1295-rtc",
		.data = (void *)(RTK_RTC_HAS_CLK_EN),
	},
	{
		.compatible = "realtek,rtd1619-rtc",
		.data = (void *)(RTK_RTK_HAS_BIAS),
	},
	{
		.compatible = "realtek,rtd1319-rtc",
		.data = (void *)(RTK_RTK_HAS_BIAS),
	},
	{}
};
MODULE_DEVICE_TABLE(of, rtk_rtc_ids);

static struct platform_driver rtk_rtc_driver = {
	.probe  = rtk_rtc_probe,
	.remove = rtk_rtc_remove,
	.driver = {
		.name = "rtk-rtc",
		.of_match_table = rtk_rtc_ids,
	},
};
module_platform_driver(rtk_rtc_driver);

MODULE_DESCRIPTION("Realtek Real-time Clock Driver");
MODULE_LICENSE("GPL v2");
