// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2017,2020 Realtek Semiconductor Corp.
 */


#include <linux/bitmap.h>
#include <linux/completion.h>
#include <linux/cpu.h>
#include <linux/cpuhotplug.h>
#include <linux/cpumask.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/slab.h>
#include <linux/thermal.h>

struct c3dev {
	struct device *dev;
	unsigned long max_state;
	atomic_t cur_state;
	struct completion complete;
	struct task_struct *task;
	struct notifier_block cpu_nb;
	struct thermal_cooling_device *cdev;
	int dyn_state;
};

static void c3dev_set_cpu_offline(struct c3dev *c, int cpu_id)
{
	struct device *cpu_dev = get_cpu_device(cpu_id);

	if (WARN_ON(cpu_id == 0))
		return;

	dev_info(c->dev, "set cpu%d offline\n", cpu_id);
	device_offline(cpu_dev);
}

static void c3dev_set_cpu_online(struct c3dev *c, int cpu_id)
{
	struct device *cpu_dev = get_cpu_device(cpu_id);

	dev_info(c->dev, "set cpu%d online\n", cpu_id);
	device_online(cpu_dev);
}

static int next_cpu_to_online(void)
{
	struct cpumask v;

	cpumask_xor(&v, cpu_possible_mask, cpu_online_mask);
	return cpumask_first(&v);
}

static int next_cpu_to_offline(void)
{
	return find_last_bit(cpumask_bits(cpu_online_mask), nr_cpumask_bits);
}

static int c3dev_get_real_state(struct c3dev *c)
{
	int v;

	get_online_cpus();
	v =  c->max_state - num_online_cpus() + 1;
	put_online_cpus();
	return v;
}

static int c3dev_do_task(void *data)
{
	struct c3dev *c = data;
	int ret;
	unsigned int cur_state, real_state;

	for (;;) {
		ret = wait_for_completion_timeout(&c->complete, 2 * HZ);
		if (kthread_should_stop()) {
			dev_info(c->dev, "stop %s\n", __func__);
			break;
		}

		reinit_completion(&c->complete);
		if (ret == 0)
			continue;

		cur_state = atomic_read(&c->cur_state);
		real_state = c3dev_get_real_state(c);

		dev_dbg(c->dev, "%s: cur_state=%d, real_state=%d\n", __func__,
			cur_state, real_state);
		if (cur_state == real_state)
			continue;

		if (cur_state < real_state)
			c3dev_set_cpu_online(c, next_cpu_to_online());
		else
			c3dev_set_cpu_offline(c, next_cpu_to_offline());

		real_state = c3dev_get_real_state(c);
		atomic_set(&c->cur_state, real_state);
		msleep(500);
	}
	return 0;
}

static int c3dev_get_max_state(struct thermal_cooling_device *cdev,
			       unsigned long *state)
{
	struct c3dev *c = cdev->devdata;

	*state = c->max_state;
	return 0;
}

static int c3dev_get_cur_state(struct thermal_cooling_device *cdev,
			       unsigned long *state)
{
	struct c3dev *c = cdev->devdata;

	*state = atomic_read(&c->cur_state);
	return 0;
}

static int c3dev_set_cur_state(struct thermal_cooling_device *cdev,
			       unsigned long state)
{
	struct c3dev *c = cdev->devdata;

	atomic_set(&c->cur_state, (unsigned int)state);
	complete(&c->complete);
	return 0;
}

static struct thermal_cooling_device_ops c3dev_cooling_ops = {
	.get_max_state = c3dev_get_max_state,
	.get_cur_state = c3dev_get_cur_state,
	.set_cur_state = c3dev_set_cur_state,
};

static DEFINE_PER_CPU(struct c3dev *, c3dev);

static int c3dev_cpu_online(unsigned int cpu)
{
	struct c3dev *c = per_cpu(c3dev, cpu);

	if (!c)
		return 0;

	complete(&c->complete);
	dev_info(c->dev, "start task from %s", __func__);
	return 0;
}

static int c3dev_cpu_offline(unsigned int cpu)
{
	struct c3dev *c = per_cpu(c3dev, cpu);

	if (!c)
		return 0;

	complete(&c->complete);
	dev_info(c->dev, "start task from %s", __func__);
	return 0;
}

static int c3dev_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct c3dev *c;
	int ret;
	int cpu;

	c = devm_kzalloc(dev, sizeof(*c), GFP_KERNEL);
	if (!c)
		return -ENOMEM;

	c->dev = dev;
	c->max_state = cpumask_weight(cpu_possible_mask) - 1;
	atomic_set(&c->cur_state, 0);
	init_completion(&c->complete);

	c->task = kthread_create(c3dev_do_task, c, "c3dev_task");
	if (IS_ERR(c->task)) {
		ret = PTR_ERR(c->task);
		dev_err(dev, "failed to create kthread: %d\n", ret);
		return ret;
	}
	kthread_bind(c->task, 0);
	wake_up_process(c->task);

	ret = cpuhp_setup_state_nocalls(CPUHP_AP_ONLINE_DYN, "c3dev:online",
		c3dev_cpu_online, c3dev_cpu_offline);
	if (ret < 0) {
		dev_err(dev, "failed to steup cpuhp state: %d\n", ret);
		return ret;
	}
	c->dyn_state = ret;

	c->cdev = thermal_of_cooling_device_register(np, "thermal-cpu-core",
						     c, &c3dev_cooling_ops);
	if (IS_ERR(c->cdev)) {
		ret = PTR_ERR(c->cdev);
		dev_err(dev, "failed to register cooling device: %d\n", ret);
		goto remove_cpuhp_state;
	}

	platform_set_drvdata(pdev, c);
	for_each_possible_cpu(cpu)
		per_cpu(c3dev, cpu) = c;

	return 0;

remove_cpuhp_state:
	cpuhp_remove_state_nocalls(c->dyn_state);
	kthread_stop(c->task);

	return ret;
}

static int c3dev_remove(struct platform_device *pdev)
{
	struct c3dev *c = platform_get_drvdata(pdev);
	int cpu;

	for_each_possible_cpu(cpu)
		per_cpu(c3dev, cpu) = NULL;
	platform_set_drvdata(pdev, NULL);
	thermal_cooling_device_unregister(c->cdev);
	cpuhp_remove_state_nocalls(c->dyn_state);
	kthread_stop(c->task);
	return 0;
}

static const struct of_device_id c3dev_ids[] = {
        { .compatible = "realtek,cpu-core-cooling" },
        {}
};

static struct platform_driver c3dev_driver = {
        .probe = c3dev_probe,
        .remove = c3dev_remove,
        .driver = {
                .owner = THIS_MODULE,
                .name = "rtk-cpu-core-cooling",
                .of_match_table = c3dev_ids,
        },
};
module_platform_driver(c3dev_driver);
