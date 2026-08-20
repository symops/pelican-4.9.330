#include <linux/pm.h>
#include <linux/pm_runtime.h>
#include <linux/pm_domain.h>

#include "rtk_pd.h"

static int rtk_pd_generic_prepare(struct device *dev)
{
	struct rtk_pd_device *pd_dev = dev_get_drvdata(dev);

	dev_info(dev, "Enter %s\n", __func__);
	rtk_pd_device_show_power_state(pd_dev);
	dev_info(dev, "Exit %s\n", __func__);
	return 0;
}

static void rtk_pd_generic_complete(struct device *dev)
{
	struct rtk_pd_device *pd_dev = dev_get_drvdata(dev);

	dev_info(dev, "Enter %s\n", __func__);
	rtk_pd_device_show_power_state(pd_dev);
	dev_info(dev, "Exit %s\n", __func__);
}

static int rtk_pd_generic_suspend(struct device *dev)
{
	struct rtk_pd_device *pd_dev = dev_get_drvdata(dev);

	dev_info(dev, "Enter %s\n", __func__);
	rtk_pd_device_show_power_state(pd_dev);
	dev_info(dev, "Exit %s\n", __func__);
	return 0;
}

static int rtk_pd_generic_resume(struct device *dev)
{
	struct rtk_pd_device *pd_dev = dev_get_drvdata(dev);

	dev_info(dev, "Enter %s\n", __func__);
	rtk_pd_device_show_power_state(pd_dev);
	dev_info(dev, "Exit %s\n", __func__);
	return 0;
}

static int rtk_pd_generic_suspend_late(struct device *dev)
{
	struct rtk_pd_device *pd_dev = dev_get_drvdata(dev);

	dev_info(dev, "Enter %s\n", __func__);
	rtk_pd_device_show_power_state(pd_dev);
	dev_info(dev, "Exit %s\n", __func__);
	return 0;
}

static int rtk_pd_generic_resume_early(struct device *dev)
{
	struct rtk_pd_device *pd_dev = dev_get_drvdata(dev);

	dev_info(dev, "Enter %s\n", __func__);
	rtk_pd_device_show_power_state(pd_dev);
	dev_info(dev, "Exit %s\n", __func__);
	return 0;
}

static int rtk_pd_generic_suspend_noirq(struct device *dev)
{
	struct rtk_pd_device *pd_dev = dev_get_drvdata(dev);

	dev_info(dev, "Enter %s\n", __func__);
	rtk_pd_device_show_power_state(pd_dev);
	dev_info(dev, "Exit %s\n", __func__);
	return 0;
}

static int rtk_pd_generic_resume_noirq(struct device *dev)
{
	struct rtk_pd_device *pd_dev = dev_get_drvdata(dev);

	dev_info(dev, "Enter %s\n", __func__);
	rtk_pd_device_show_power_state(pd_dev);
	dev_info(dev, "Exit %s\n", __func__);
	return 0;
}

const struct dev_pm_ops rtk_pd_generic_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(rtk_pd_generic_suspend,
				rtk_pd_generic_resume)
	SET_LATE_SYSTEM_SLEEP_PM_OPS(rtk_pd_generic_suspend_late,
				     rtk_pd_generic_resume_early)
	SET_NOIRQ_SYSTEM_SLEEP_PM_OPS(rtk_pd_generic_suspend_noirq,
				      rtk_pd_generic_resume_noirq)
	.prepare                    = rtk_pd_generic_prepare,
	.complete                   = rtk_pd_generic_complete,
};
