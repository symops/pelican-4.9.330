/*
 *  mycloud_pstore.c - Driver to instantiate mycloud ramoops device
 *
 *  Copyright (C) 2016 WD
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 2 of the License.
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pstore_ram.h>
#include <linux/memblock.h>

#define RAM_CONSOLE_ADDR (UL(0x10000000))

/*
 * define the memory to store pstore contents for panic logs, etc.
 */
static struct ramoops_platform_data mycloud_ramoops_data = {
    .mem_size = 0x80000,
    .mem_address = RAM_CONSOLE_ADDR,
    .mem_type = 0x1,
    .record_size = 0x80000,
    .dump_oops = 1,
};

static struct platform_device mycloud_ramoops = {
    .name = "ramoops",
    .dev = {
        .platform_data = &mycloud_ramoops_data,
    },
};

static int __init mycloud_pstore_init(void)
{
    int ret;

    ret = platform_device_register(&mycloud_ramoops);
    if (ret)
    {
        pr_err("%s: unable to register ram console device:"
               "start=0x%08x, size=0x%08x, ret=%d\n",
               __func__, (u32)mycloud_ramoops_data.mem_address,
               (u32)mycloud_ramoops_data.mem_size, ret);
        return ret;
    }

    if (mycloud_ramoops_data.mem_address)
    {
        if (memblock_is_region_reserved(mycloud_ramoops_data.mem_address, mycloud_ramoops_data.mem_size) ||
            memblock_reserve(mycloud_ramoops_data.mem_address, mycloud_ramoops_data.mem_size) < 0)
        {
            pr_err("%s: cannot reserve memory block at 0x%x\n", __func__, mycloud_ramoops_data.mem_address);
            return -EBUSY;
        }
    }
}

static void __exit mycloud_pstore_exit(void)
{
    platform_device_unregister(&mycloud_ramoops);
}

module_init(mycloud_pstore_init);
module_exit(mycloud_pstore_exit);

MODULE_DESCRIPTION("Chrome OS pstore module");
MODULE_LICENSE("GPL");
