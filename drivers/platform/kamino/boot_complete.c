/*
 * boot_complete.c
 *
 * Boot Complete driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/vmalloc.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/string.h>
#include <linux/sched.h>
#include <linux/uaccess.h>
#include <linux/io.h>
#include <linux/random.h>
#include <asm/uaccess.h>
#include <linux/time.h>
#include "kamino.h"

#define BOOT_COMPLETE_VERSION "1.0"
#define BOOT_COMPLETE_PROC_NAME "boot_completed"
#define BOOT_TIME_PROC_NAME "boot_time"
#define BID_PROC_NAME "bid"
#define MAX_SIZE 20

struct dev_boot_complete
{
    u8 boot_completed;
    long boot_time;
    unsigned int bid;
    struct mutex lock;
};

static struct dev_boot_complete *p_dev_boot_complete;

static int boot_complete_show(struct seq_file *m, void *v)
{
    if (p_dev_boot_complete)
    {
        seq_printf(m, "%d", p_dev_boot_complete->boot_completed);
    }
    else
        seq_printf(m, "boot complete driver is not initialized");
    return 0;
}

static int boot_complete_proc_open(struct inode *inode, struct file *file)
{
    return single_open(file, boot_complete_show, NULL);
}

static ssize_t boot_complete_proc_write(struct file *file, const char *buf, size_t size, loff_t *ppos)
{
    size_t len = MAX_SIZE;
    char mbuf[MAX_SIZE + 1];
    struct timespec boot_time;

    if (len > size)
        len = size;

    //once boot completed, don't take write anymore
    if (p_dev_boot_complete->boot_completed)
        return len;

    if (copy_from_user(mbuf, buf, len))
        return -EFAULT;

    mbuf[len] = '\0';
    printk(KERN_ERR "%s: the proc write is %s len is %d\n", __func__, mbuf, (int)len);

    if (!strcmp(mbuf, "true"))
    {
        printk(KERN_ERR "%s: matches\n", __func__);
        p_dev_boot_complete->boot_completed = 1;
        ktime_get_ts(&boot_time);
        p_dev_boot_complete->boot_time = boot_time.tv_sec;
        get_random_bytes(&p_dev_boot_complete->bid, sizeof(unsigned int));
        KAMINO_LOG("boot_time=%ld\n", boot_time.tv_sec);
    }

    return len;
}

unsigned int boot_complete_get_bid(void)
{
    return p_dev_boot_complete->bid;
}

static const struct file_operations proc_boot_complete_fops = {
    .open = boot_complete_proc_open,
    .read = seq_read,
    .write = boot_complete_proc_write,
    .llseek = seq_lseek,
    .release = single_release,
};

static int boot_time_show(struct seq_file *m, void *v)
{
    if (p_dev_boot_complete)
    {
        seq_printf(m, "%ld", p_dev_boot_complete->boot_time);
    }
    else
        seq_printf(m, "boot complete driver is not initialized");
    return 0;
}

static int boot_time_proc_open(struct inode *inode, struct file *file)
{
    return single_open(file, boot_time_show, NULL);
}

static const struct file_operations proc_boot_time_fops = {
    .open = boot_time_proc_open,
    .read = seq_read,
    .llseek = seq_lseek,
    .release = single_release,
};

static int bid_show(struct seq_file *m, void *v)
{
    if (p_dev_boot_complete)
    {
        seq_printf(m, "%u", p_dev_boot_complete->bid);
    }
    else
        seq_printf(m, "boot complete driver is not initialized");
    return 0;
}

static int bid_proc_open(struct inode *inode, struct file *file)
{
    return single_open(file, bid_show, NULL);
}

static const struct file_operations proc_bid_fops = {
    .open = bid_proc_open,
    .read = seq_read,
    .llseek = seq_lseek,
    .release = single_release,
};

void boot_complete_proc_init(void)
{
    struct proc_dir_entry *boot_complete_entry, *boot_time_entry, *bid_entry;
    printk(KERN_ERR "%s: create proc\n", __func__);
    boot_complete_entry = proc_create(BOOT_COMPLETE_PROC_NAME,
                                      0644, NULL, &proc_boot_complete_fops);
    if (boot_complete_entry == NULL)
    {
        printk(KERN_ERR "%s: Can't create boot_complete proc entry\n", __func__);
        return;
    }

    boot_time_entry = proc_create(BOOT_TIME_PROC_NAME,
                                  0444, NULL, &proc_boot_time_fops);
    if (boot_time_entry == NULL)
    {
        printk(KERN_ERR "%s: Can't create boot_time proc entry\n", __func__);
        return;
    }

    bid_entry = proc_create(BID_PROC_NAME,
                            0444, NULL, &proc_bid_fops);
    if (bid_entry == NULL)
    {
        printk(KERN_ERR "%s: Can't create bid proc entry\n", __func__);
        return;
    }
}
EXPORT_SYMBOL(boot_complete_proc_init);

void boot_complete_proc_done(void)
{
    remove_proc_entry(BOOT_COMPLETE_PROC_NAME, NULL);
}
EXPORT_SYMBOL(boot_complete_proc_done);

static int __init device_boot_complete_init(void)
{
    printk(KERN_ERR "%s: boot complete init\n", __func__);
    p_dev_boot_complete = kzalloc(sizeof(struct dev_boot_complete), GFP_KERNEL);
    if (!p_dev_boot_complete)
    {
        printk(KERN_ERR "%s: kmalloc allocation failed\n", __func__);
        return -ENOMEM;
    }

    /* set the default boot completed */
    p_dev_boot_complete->boot_completed = 0;

    /* create the proc entry for boot complete */
    boot_complete_proc_init();

    return 0;
}

static void __exit dev_boot_complete_cleanup(void)
{
    boot_complete_proc_done();
    if (p_dev_boot_complete)
        kfree(p_dev_boot_complete);
}

module_init(device_boot_complete_init);
module_exit(dev_boot_complete_cleanup);
MODULE_LICENSE("GPL v2");
