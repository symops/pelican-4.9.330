#include <linux/clk.h>
#include <linux/cma.h>
#include <linux/dma-buf.h>
#include <linux/dma-mapping.h>
#include <linux/fs.h>
#include <linux/interrupt.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/reset.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/uaccess.h>
#include <linux/wait.h>
#include <linux/overflow.h>

#include "rtk_se.h"
#include "uapi/rtk_se.h"

struct se_device {
	struct miscdevice         mdev;
	void                      *base;
	struct device             *dev;
	struct clk                *clk;
	struct reset_control      *rstc;

	atomic_t                  dist;

	int                       instance_idmap[3];
	int                       num_instances;
	struct se_instance        ins[3];
};

static void se_reg_write(struct se_device *sedev, uint32_t offset, uint32_t val)
{
	writel(val, sedev->base + offset);
}

static uint32_t se_reg_read(struct se_device *sedev, uint32_t offset)
{
	return readl(sedev->base + offset);
}

int se_cmdq_add_cmd(struct se_cmdq *cmdq, struct se_cmd *cmd)
{
	if (cmdq->pos + sizeof(*cmd) > cmdq->size)
		return -EINVAL;

	memcpy(cmdq->virt + cmdq->pos, cmd, sizeof(*cmd));
	cmdq->pos += sizeof(*cmd);

	dev_dbg(cmdq->ins->core->dev, "cmd = [%08x %08x %08x %08x %08x %08x %08x %08x]\n",
		cmd->cmds[0], cmd->cmds[1], cmd->cmds[2], cmd->cmds[3],
		cmd->cmds[4], cmd->cmds[5], cmd->cmds[6], cmd->cmds[7]);

	return 0;
}

void se_cmdq_reset(struct se_cmdq *cmdq)
{
	cmdq->pos = 0;
}

static int se_instance_alloc_cmdq(struct se_instance *ins, struct se_cmdq *cmdq)
{
	struct device *dev = ins->core->dev;

	cmdq->ins = ins;
	cmdq->pos = 0;
	cmdq->size = PAGE_SIZE;
	cmdq->virt = dma_alloc_coherent(dev, cmdq->size, &cmdq->dma, GFP_KERNEL | GFP_DMA);
	dev_dbg(dev, "%s: phys:%pad, virt=%p\n", __func__, &cmdq->dma, cmdq->virt);
	return cmdq->virt ? 0 : -ENOMEM;
}

static void se_instance_free_cmdq(struct se_instance *ins, struct se_cmdq *cmdq)
{
	struct device *dev = ins->core->dev;

	dma_free_coherent(dev, cmdq->size, cmdq->virt, cmdq->dma);
}

static void se_instance_setup_cmdq(struct se_instance *ins, struct se_cmdq *cmdq)
{
	struct device *dev = ins->core->dev;

	dma_sync_single_for_device(dev, cmdq->dma, cmdq->size, DMA_TO_DEVICE);

	se_reg_write(ins->core, SE_CMDBASE_OFFSET(ins->id), cmdq->dma);
	se_reg_write(ins->core, SE_CMDLMT_OFFSET(ins->id),  cmdq->dma + cmdq->size);
	se_reg_write(ins->core, SE_CMDRPTR_OFFSET(ins->id), cmdq->dma);
	se_reg_write(ins->core, SE_CMDWPTR_OFFSET(ins->id), cmdq->dma + cmdq->pos);
}

enum {
	SE_INT_COM_ERROR = 0x00000004,
	SE_INT_COM_EMPTY = 0x00000008,
	SE_INT_FMT_ERR   = 0x00010000,
};

static void se_instance_enable_int(struct se_instance *ins)
{
	 se_reg_write(ins->core, SE_INTE_OFFSET(ins->id),
		SE_INT_COM_ERROR | SE_INT_COM_EMPTY | SE_INT_FMT_ERR | 1);
}

static void se_instance_disable_int(struct se_instance *ins)
{
	se_reg_write(ins->core, SE_INTE_OFFSET(ins->id), ~1);
}

static void se_instance_start(struct se_instance *ins)
{
	se_reg_write(ins->core, SE_CTRL_OFFSET(ins->id), 0x7);
}

static void se_instance_stop(struct se_instance *ins)
{
	se_reg_write(ins->core, SE_CTRL_OFFSET(ins->id), 2);
}

static uint32_t se_instance_get_ints(struct se_instance *ins)
{
	return se_reg_read(ins->core, SE_INTS_OFFSET(ins->id));
}

static void se_instance_clear_ints(struct se_instance *ins)
{
	se_reg_write(ins->core, SE_INTS_OFFSET(ins->id), ~1);
}

static void se_instance_check_ints(struct se_instance *ins)
{
	uint32_t ints;

	ints = se_instance_get_ints(ins);
	if (ints == 0)
		return;
	se_instance_clear_ints(ins);
	ins->status = ints;

	ins->cmd_done = true;
	wake_up_interruptible(&ins->cmd_done_wait);

	se_instance_stop(ins);

	se_instance_disable_int(ins);
}

static int se_instance_wait_cmd_done(struct se_instance *ins)
{
	int ret;

	ret = wait_event_interruptible_timeout(ins->cmd_done_wait,
		(ins->cmd_done), msecs_to_jiffies(500));

	if (ret == 0)
		ret = -ETIMEDOUT;

	return ret < 0 ? ret : 0;
}

static int se_instance_submit(struct se_instance *ins, unsigned long flags)
{
	struct device *dev = ins->core->dev;
	int ret = 0;

	pm_runtime_get_sync(dev);

	se_instance_setup_cmdq(ins, &ins->cmdq);

	dev_dbg(dev, "init cmdq_reg = [%08x,%08x,%08x,%08x]\n",
		se_reg_read(ins->core, SE_CMDBASE_OFFSET(ins->id)),
		se_reg_read(ins->core, SE_CMDLMT_OFFSET(ins->id)),
		se_reg_read(ins->core, SE_CMDRPTR_OFFSET(ins->id)),
		se_reg_read(ins->core, SE_CMDWPTR_OFFSET(ins->id)));

	ins->cmd_done = false;
	ins->status = 0;

	se_instance_enable_int(ins);

	se_instance_start(ins);

	ret = se_instance_wait_cmd_done(ins);
	if (ret == -ETIMEDOUT)
		dev_err(dev, "command timeout\n");
	if (ins->status & ~SE_INT_COM_EMPTY) {
		dev_err(dev, "command error: ints=%08x\n", ins->status);
		ret = -EINVAL;
	}

	dev_dbg(dev, "cmdq_reg = [%08x,%08x,%08x,%08x], idle=%08x, status=%08x\n",
		se_reg_read(ins->core, SE_CMDBASE_OFFSET(ins->id)),
		se_reg_read(ins->core, SE_CMDLMT_OFFSET(ins->id)),
		se_reg_read(ins->core, SE_CMDRPTR_OFFSET(ins->id)),
		se_reg_read(ins->core, SE_CMDWPTR_OFFSET(ins->id)),
		se_reg_read(ins->core, SE_IDLE_OFFSET(ins->id)),
		ins->status);

	pm_runtime_put_sync(dev);

	return ret;
}

static int se_instance_acquire(struct se_instance *ins)
{
	int ret;

	ret = mutex_lock_interruptible(&ins->lock);
	if (ret)
		return ret;
	return 0;
}

static void se_instance_release(struct se_instance *ins)
{
	mutex_unlock(&ins->lock);
}

static int se_instance_init(struct se_device *sedev, struct se_instance *ins, int id)
{
	int ret;

	ins->core = sedev;
	ins->id = id;
	mutex_init(&ins->lock);

	ret = se_instance_alloc_cmdq(ins, &ins->cmdq);
	if (ret)
		return ret;

	init_waitqueue_head(&ins->cmd_done_wait);
	ins->cmd_done = false;

	se_instance_stop(ins);
	se_instance_setup_cmdq(ins, &ins->cmdq);
	return 0;
}

static void se_instance_fini(struct se_instance *ins)
{
	se_instance_free_cmdq(ins, &ins->cmdq);
}

struct se_file_data {
	struct se_device    *core;
	struct se_instance  *ins;
};

struct se_dma_buf {
	struct dma_buf_attachment   *import_attach;
	struct sg_table             *sgt;
	dma_addr_t                  dma_addr;
	unsigned int                dma_len;
};

static int file_check_perm(struct file *file, bool rd)
{
	unsigned int flags;

	if (!file)
		return -EBADF;

	flags = file->f_flags & O_ACCMODE;

	if (flags == O_RDWR)
		return 0;

	return (rd && flags == O_RDONLY) || (!rd && flags == O_WRONLY) ? 0 : -EPERM;
}

static int se_dma_buf_import(struct se_device *se_dev, struct se_dma_buf *mobj, int fd, bool rd)
{
	struct device *dev = se_dev->dev;
	struct dma_buf *buf = dma_buf_get(fd);
	struct dma_buf_attachment *attach;
	struct sg_table *sgt;
	dma_addr_t dma_addr;
	int ret = 0;

	if (IS_ERR(buf))
		return PTR_ERR(buf);

	ret = file_check_perm(buf->file, rd);
	if (ret) {
		dev_err(dev, "no permission: %d\n", ret);
		return ret;
	}


	attach = dma_buf_attach(buf, se_dev->dev);
	if (IS_ERR(attach)) {
		dev_err(dev, "failed to attach dma-buf: %d\n", ret);
		ret = PTR_ERR(attach);
		goto put_dma_buf;
	}

	sgt = dma_buf_map_attachment(attach, DMA_BIDIRECTIONAL);
	if (IS_ERR(sgt)) {
		ret = PTR_ERR(sgt);
		dev_err(dev, "failed to map attachment: %d\n", ret);
		goto detach_dma_buf;
	}


	if (sgt->nents != 1) {
		ret = -EINVAL;
		dev_err(dev, "scatter list not supportted\n");
		goto detach_dma_buf;
	}

	dma_addr = sg_dma_address(sgt->sgl);
	if (!dma_addr) {
		ret = -EINVAL;
		dev_err(dev, "invalid dma address\n");
		goto detach_dma_buf;
	}

	mobj->import_attach = attach;
	mobj->sgt           = sgt;
	mobj->dma_addr      = dma_addr;
	mobj->dma_len       = sg_dma_len(sgt->sgl);
	return 0;

detach_dma_buf:
	dma_buf_detach(buf, attach);
put_dma_buf:
	dma_buf_put(buf);
	return ret;
}

static void se_dma_buf_release(struct se_dma_buf *mobj)
{
	struct dma_buf_attachment *attach = mobj->import_attach;
	struct dma_buf *buf;

	if (mobj->sgt)
		dma_buf_unmap_attachment(attach, mobj->sgt, DMA_BIDIRECTIONAL);

	buf = attach->dmabuf;
	dma_buf_detach(buf, attach);
	dma_buf_put(buf);
}

static inline int validate_memcpy_args(uint32_t dma_len, uint32_t offset, uint32_t size)
{
	if (check_add_overflow(offset, size, &size))
		return -EINVAL;
	return dma_len < size ? -EINVAL : 0;
}

static int se_ioctl_cmd_memcpy(struct se_instance *ins, struct rtk_se_memcpy *cmd)
{
	struct device *dev = ins->core->dev;
	struct se_dma_buf src = { 0 }, dst = { 0 };
	int ret;
	dma_addr_t dst_addr, src_addr;
	uint32_t dst_size, src_size;

	if (cmd->mode == 0)
		dst_size = src_size = cmd->size;
	else {
		if (check_mul_overflow(cmd->height, cmd->dst_stride, &dst_size) ||
		    check_mul_overflow(cmd->height, cmd->src_stride, &src_size))
			return -EINVAL;
	}

	ret = se_dma_buf_import(ins->core, &dst, cmd->dst_fd, false);
	if (ret) {
		dev_err(dev, "failed to import dst: %d\n", ret);
		return ret;
	}

	ret = se_dma_buf_import(ins->core, &src, cmd->src_fd, true);
	if (ret) {
		dev_err(dev, "failed to import src: %d\n", ret);
		goto release_dst;
	}

	if (validate_memcpy_args(dst.dma_len, cmd->dst_offset, dst_size) ||
	    validate_memcpy_args(src.dma_len, cmd->src_offset, src_size))
		goto release_src;

	dst_addr = dst.dma_addr + cmd->dst_offset;
	src_addr = src.dma_addr + cmd->src_offset;

	if (cmd->mode == 0) {
		ret = se_cmdq_add_memcpy(&ins->cmdq, dst_addr, src_addr, cmd->size);

	} else {
		ret = se_cmdq_add_memcpy_2d(&ins->cmdq, dst_addr, src_addr,
					cmd->dst_stride, cmd->src_stride,
					cmd->width, cmd->height);
	}

	if (!ret)
		ret = se_instance_submit(ins, 0);

	se_cmdq_reset(&ins->cmdq);

release_src:
	se_dma_buf_release(&src);
release_dst:
	se_dma_buf_release(&dst);
	return ret;
}

struct mem_data {
	unsigned long start;
	unsigned long end;
};

static int check_cma_range(struct cma *cma, void *data)
{
	unsigned long start = cma_get_base(cma);
	unsigned long end = start + cma_get_size(cma);
	struct mem_data *mem = data;

	if (mem->start < start || mem->end > end)
		return 0;
	return 1;
}

static int mem_is_in_cma(dma_addr_t start, size_t size)
{
	struct mem_data mem = {
		.start = start,
		.end = start + size,
	};
	return cma_for_each_area(check_cma_range, &mem);
}

static int mem_is_in_rtk_ion(dma_addr_t start, size_t size)
{
	struct mem_data mem = {
		.start = start,
                .end = start + size,
        };
	struct device_node *np, *child;
	int ret = 0;
	int len;
	int i;
	const __be32 *p;

	np = of_find_compatible_node(NULL, NULL, "Realtek,rtk-ion");
	if (!np)
		return 0;

	for_each_child_of_node(np, child) {

		p = of_get_property(child, "rtk,memory-reserve", &len);
		if (!p || (len % 12) != 0)
			continue;
		len /= 12;

		for (i = 0; i < len; i++, p+=3) {
			unsigned long start = be32_to_cpup(p);
			unsigned long end = be32_to_cpup(p+1);
			end += start;

			if (mem.start < start || mem.end > end)
				continue;
			ret = 1;
			goto found;
		}
	}
found:
	of_node_put(np);
	return ret;
}


static int mem_is_valid(dma_addr_t start, size_t size)
{
	return mem_is_in_cma(start, size) || mem_is_in_rtk_ion(start, size);
}

static int se_ioctl_cmd_memcpy_phys(struct se_instance *ins, struct rtk_se_memcpy_phys *cmd)
{
	dma_addr_t dst_addr = cmd->dst;
	dma_addr_t src_addr = cmd->src;
	int ret;
	uint32_t dst_size, src_size;

	if (cmd->mode == 0) {
		dst_size = src_size = cmd->size;
	} else {
		if (check_mul_overflow(cmd->height, cmd->dst_stride, &dst_size) ||
		    check_mul_overflow(cmd->height, cmd->src_stride, &src_size))
			return -EINVAL;
	}

	if (!mem_is_valid(dst_addr, dst_size) ||
	    !mem_is_valid(src_addr, src_size))
		return -EINVAL;

	if (cmd->mode == 0) {
		ret = se_cmdq_add_memcpy(&ins->cmdq, dst_addr, src_addr, cmd->size);
	} else {
		ret = se_cmdq_add_memcpy_2d(&ins->cmdq, dst_addr, src_addr,
					cmd->dst_stride, cmd->src_stride,
					cmd->width, cmd->height);

	}
	if (!ret)
		ret = se_instance_submit(ins, 0);
	se_cmdq_reset(&ins->cmdq);
	return ret;
}

static long se_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct se_file_data *data = filp->private_data;
	struct se_instance *ins = data->ins;
	int ret;
	union {
		struct rtk_se_memcpy memcpy;
		struct rtk_se_memcpy_phys memcpy_phys;
	} args;

	switch (cmd) {
	case RTK_SE_IOC_MEMCPY:
		ret = copy_from_user(&args.memcpy, (unsigned int __user *)arg, sizeof(struct rtk_se_memcpy));
		if (ret)
			return ret;

		ret = se_instance_acquire(ins);
		if (ret)
			return ret;

		ret = se_ioctl_cmd_memcpy(ins, &args.memcpy);

		se_instance_release(ins);
		break;

	case RTK_SE_IOC_MEMCPY_PHYS:
		ret = copy_from_user(&args.memcpy, (unsigned int __user *)arg, sizeof(struct rtk_se_memcpy_phys));
		if (ret)
			return ret;

		ret = se_instance_acquire(ins);
		if (ret)
			return ret;

		ret = se_ioctl_cmd_memcpy_phys(ins, &args.memcpy_phys);

		se_instance_release(ins);
		break;

	default:
		return -ENOIOCTLCMD;
	}

	return ret;
}

#ifdef CONFIG_COMPAT
static long se_compact_ioctl(struct file *filp, unsigned int cmd,
			      unsigned long arg)
{
	return se_ioctl(filp, cmd, (unsigned long)compat_ptr(arg));
}
#endif

static struct se_instance *se_get_instance_dist(struct se_device *sedev)
{
	int v = atomic_inc_return(&sedev->dist);

	v %= sedev->num_instances;
	return &sedev->ins[v];
}

static int se_open(struct inode *inode, struct file *filp)
{
	struct se_device *sedev = container_of(filp->private_data,
		struct se_device, mdev);
	struct se_file_data *data;

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->core = sedev;
	data->ins = se_get_instance_dist(sedev);
	filp->private_data = data;
	return 0;
}

static int se_release(struct inode *inode, struct file *filp)
{
	struct se_file_data *data = filp->private_data;

	kfree(data);
	return 0;
}

const struct file_operations se_fops = {
	.owner          = THIS_MODULE,
	.open           = se_open,
	.unlocked_ioctl = se_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl   = se_compact_ioctl,
#endif
	.release        = se_release,
};

static int se_runtime_suspend(struct device *dev)
{
	struct se_device *sedev = dev_get_drvdata(dev);

	dev_dbg(dev, "%s\n", __func__);
	clk_disable_unprepare(sedev->clk);
	reset_control_assert(sedev->rstc);
	return 0;
}

static int se_runtime_resume(struct device *dev)
{
	struct se_device *sedev = dev_get_drvdata(dev);

	dev_dbg(dev, "%s\n", __func__);
	reset_control_deassert(sedev->rstc);
	clk_prepare_enable(sedev->clk);
	return 0;
}

static const struct dev_pm_ops se_pm_ops = {
	.runtime_suspend = se_runtime_suspend,
	.runtime_resume  = se_runtime_resume,
};

static irqreturn_t se_interrupt(int irq, void *dev_id)
{
	struct se_device *sedev = dev_id;
	int i;

	for (i = 0; i < sedev->num_instances; i++)
		se_instance_check_ints(&sedev->ins[i]);
	return IRQ_HANDLED;
}

static int se_setup_instances(struct se_device *sedev)
{
	int i;

	for (i = 0; i < sedev->num_instances; i++)
		se_instance_init(sedev, &sedev->ins[i], sedev->instance_idmap[i]);
	return 0;
}

static void se_remove_instances(struct se_device *sedev)
{
	int i;

	for (i = 0; i < sedev->num_instances; i++)
		se_instance_fini(&sedev->ins[i]);
}

static int se_of_parse_instance_id_map(struct se_device *sedev)
{
	struct device_node *np = sedev->dev->of_node;
	int len;
	int ret;

	if (!of_find_property(np, "id-map", &len))
		goto default_idmap;

	if (len == 0 || len > 12 || (len % 4) != 0)
		goto default_idmap;
	len /= 4;

	sedev->num_instances = len;
	ret = of_property_read_u32_array(np, "id-map", sedev->instance_idmap, len);
	if (ret)
		goto default_idmap;

	return 0;

default_idmap:
	sedev->instance_idmap[0] = 0;
	sedev->num_instances = 1;
	return 0;
}

static int se_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct se_device *sedev;
	struct resource res;
	int ret;

	sedev = devm_kzalloc(dev, sizeof(*sedev), GFP_KERNEL);
	if (!sedev)
		return -ENOMEM;

	sedev->dev = dev;
	platform_set_drvdata(pdev, sedev);

	ret = of_address_to_resource(dev->of_node, 0, &res);
	if (ret) {
		dev_err(dev, "failed to get recource: %d\n", ret);
		return -ENODEV;
	}

	sedev->base = devm_ioremap(dev, res.start, resource_size(&res));
	if (!sedev->base)
		return -ENOMEM;

	sedev->clk = devm_clk_get(dev, NULL);
	if (IS_ERR(sedev->clk)) {
		ret = PTR_ERR(sedev->clk);
		dev_err(dev, "failed to get clk: %d\n", ret);
		return ret;
	}

	sedev->rstc = devm_reset_control_get(dev, NULL);
	if (IS_ERR(sedev->rstc)) {
		ret = PTR_ERR(sedev->rstc);
		dev_err(dev, "failed to get reset control: %d\n", ret);
		return ret;
	}

	ret = platform_get_irq(pdev, 0);
	if (ret < 0) {
		dev_err(dev, "failed to get irq: %d\n", ret);
		return ret;
	}

	ret = devm_request_irq(dev, ret, se_interrupt, IRQF_SHARED,
			       dev_name(dev), sedev);
	if (ret) {
		dev_err(dev, "failed to request irq: %d\n", ret);
		return ret;
	}

	se_of_parse_instance_id_map(sedev);

	sedev->mdev.minor  = MISC_DYNAMIC_MINOR;
	sedev->mdev.name   = "se";
	sedev->mdev.fops   = &se_fops;
	sedev->mdev.parent = NULL;
	ret = misc_register(&sedev->mdev);
	if (ret) {
		dev_err(dev, "failed to register misc device: %d\n", ret);
		return ret;
	}

	pm_runtime_set_suspended(dev);
	pm_runtime_enable(dev);

	pm_runtime_get_sync(dev);
	pm_runtime_forbid(dev);
	se_setup_instances(sedev);
	pm_runtime_put_sync(dev);
	return 0;
}

static int se_remove(struct platform_device *pdev)
{
	struct se_device *sedev = platform_get_drvdata(pdev);

	platform_set_drvdata(pdev, NULL);
	se_remove_instances(sedev);
	pm_runtime_disable(sedev->dev);
	return 0;
}

static const struct of_device_id se_ids[] = {
	{ .compatible = "realtek,streaming-engine" },
	{}
};

static struct platform_driver se_driver = {
	.probe  = se_probe,
	.remove = se_remove,
	.driver = {
		.name           = "rtk-se-dma",
		.owner          = THIS_MODULE,
		.of_match_table = se_ids,
		.pm             = &se_pm_ops,
	},
};
module_platform_driver(se_driver);
