#include <linux/errno.h>
#include "rtk_se.h"

#define to_ins_id(_cmdq) ((_cmdq)->ins->id)

static int se_cmdq_add_write_reg(struct se_cmdq *cmdq,
		uint32_t reg_addr, uint32_t value)
{
	struct se_cmd cmd;

	if (WARN_ON(reg_addr > 0xfff))
		return -EINVAL;

	se_cmd_setup_write_reg(&cmd, reg_addr, value);

	return se_cmdq_add_cmd(cmdq, &cmd) ? 1 : 0;
}

static int se_cmdq_add_copy(struct se_cmdq *cmdq,
		uint32_t dst_sel, uint32_t dst_x, uint32_t dst_y,
		uint32_t src_sel, uint32_t src_x, uint32_t src_y,
		uint32_t width, uint32_t height)
{
	struct se_cmd cmd;

	if (WARN_ON(width >= 4096))
		return -EINVAL;

	if (WARN_ON(height >= 4096))
		return -EINVAL;

	se_cmd_setup_copy(&cmd, dst_sel, dst_x, dst_y, src_sel, src_x, src_y, width, height, 0, 0);

	return se_cmdq_add_cmd(cmdq, &cmd) ? 1 : 0;
}

int se_cmdq_add_memcpy(struct se_cmdq *cmdq, dma_addr_t dst, dma_addr_t src, uint32_t size)
{
	uint32_t width, height, stride, copy_size;
	int ret, ec = 0;

	ret = se_cmdq_add_write_reg(cmdq, SE_CLR_FMT_OFFSET(to_ins_id(cmdq)), se_reg_clr_fmt_val(SE_CLR_FMT_RGB332, 0, 0, 0, 0, 1));
	ec += ret != 0;

	while (size != 0) {
		if (size >= (4096 * 4096)) {
			width = height = 0;
			stride = 4096;
			copy_size = 4096 * 4096;
		} else if (size >= 4096) {
			width = 0;
			height = size >> 12;
			stride = 4096;
			copy_size = 4096 * height;
		} else {
			width = stride = copy_size = size;
			height = 1;
		}

		ret = se_cmdq_add_write_reg(cmdq, SE_BADDR_OFFSET(to_ins_id(cmdq), 0), dst);
		ec += ret != 0;

		ret = se_cmdq_add_write_reg(cmdq, SE_PTICH_OFFSET(to_ins_id(cmdq), 0), se_reg_pitch_val(stride, 0));
		ec += ret != 0;

		ret = se_cmdq_add_write_reg(cmdq, SE_BADDR_OFFSET(to_ins_id(cmdq), 1), src);
		ec += ret != 0;

		ret = se_cmdq_add_write_reg(cmdq, SE_PTICH_OFFSET(to_ins_id(cmdq), 1), se_reg_pitch_val(stride, 0));
		ec += ret != 0;

		ret = se_cmdq_add_copy(cmdq, 0, 0, 0, 1, 0, 0, width, height);
		ec += ret != 0;

		dst += copy_size;
		src += copy_size;
		size -= copy_size;
	}

	return ec > 0 ? -EINVAL : 0;
}

int se_cmdq_add_memcpy_2d(struct se_cmdq *cmdq, dma_addr_t dst, dma_addr_t src,
		uint32_t dst_stride, uint32_t src_stride,
		uint32_t width,	uint32_t height)
{
	int ret, ec = 0;

	ret = se_cmdq_add_write_reg(cmdq, SE_CLR_FMT_OFFSET(to_ins_id(cmdq)), se_reg_clr_fmt_val(SE_CLR_FMT_RGB332, 0, 0, 0, 0, 1));
	ec += ret != 0;

	ret = se_cmdq_add_write_reg(cmdq, SE_BADDR_OFFSET(to_ins_id(cmdq), 0), dst);
	ec += ret != 0;

	ret = se_cmdq_add_write_reg(cmdq, SE_PTICH_OFFSET(to_ins_id(cmdq), 0), se_reg_pitch_val(dst_stride, 0));
	ec += ret != 0;

	ret = se_cmdq_add_write_reg(cmdq, SE_BADDR_OFFSET(to_ins_id(cmdq), 1), src);
	ec += ret != 0;

	ret = se_cmdq_add_write_reg(cmdq, SE_PTICH_OFFSET(to_ins_id(cmdq), 1), se_reg_pitch_val(src_stride, 0));
	ec += ret != 0;

	ret = se_cmdq_add_copy(cmdq, 0, 0, 0, 1, 0, 0, width, height);
	ec += ret != 0;

	return ec > 0 ? -EINVAL : 0;
}
