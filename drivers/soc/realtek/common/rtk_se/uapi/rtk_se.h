/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */

#ifndef _UAPI_LINUX_RTK_SE_H
#define _UAPI_LINUX_RTK_SE_H

#include <linux/types.h>

struct rtk_se_memcpy {
	int       dst_fd;
	__u32     dst_offset;
	int       src_fd;
	__u32     src_offset;
	__u32     mode;
	union {
		__u32 size;
		struct {
			__u32 dst_stride;
			__u32 src_stride;
			__u32 width;
			__u32 height;
		};
	};
};

struct rtk_se_memcpy_phys {
	__u32     dst;
	__u32     src;
	__u32     mode;
	union {
		__u32 size;
		struct {
			__u32 dst_stride;
			__u32 src_stride;
			__u32 width;
			__u32 height;
		};
	};
};

#define RTK_SE_IOC_MAGIC           's'
#define RTK_SE_IOC_MEMCPY          _IOWR(RTK_SE_IOC_MAGIC, 0x01, struct rtk_se_memcpy)
#define RTK_SE_IOC_MEMCPY_PHYS     _IOWR(RTK_SE_IOC_MAGIC, 0x21, struct rtk_se_memcpy_phys)

#endif
