#ifndef __SOC_REALTEK_SE_H
#define __SOC_REALTEK_SE_H

#include <linux/bitops.h>
#include <linux/types.h>
#include <linux/wait.h>
#include <linux/mutex.h>

#define SE_CMDBASE_OFFSET(_ins)     (0x000 + (_ins) * 4)
#define SE_CMDLMT_OFFSET(_ins)      (0x00C + (_ins) * 4)
#define SE_CMDRPTR_OFFSET(_ins)     (0x018 + (_ins) * 4)
#define SE_CMDWPTR_OFFSET(_ins)     (0x024 + (_ins) * 4)
#define SE_SRWORDCNT_OFFSET(_ins)   (0x030 + (_ins) * 4)
#define SE_Q_PRIORITY               (0x03C)

#define SE_CTRL_OFFSET(_ins)        (0x458 + (_ins) * 4)
#define SE_IDLE_OFFSET(_ins)        (0x464 + (_ins) * 4)
#define SE_INTSEL                   (0x470)
#define SE_INTS_OFFSET(_ins)        (0x474 + (_ins) * 4)
#define SE_INTE_OFFSET(_ins)        (0x480 + (_ins) * 4)
#define SE_INSTCNT_L_OFFSET(_ins)   (0x48C + (_ins) * 4)
#define SE_INSTCNT_H_OFFSET(_ins)   (0x510 + (_ins) * 4)

#define SE_CLR_FMT_OFFSET(_ins)     (0x040 + (_ins) * 4)
#define SE_BADDR_OFFSET(_ins, _ch)  (0x094 + (_ins) * 0x20 + (_ch) * 4)
#define SE_PTICH_OFFSET(_ins, _ch)  (0x0A4 + (_ins) * 0x20 + (_ch) * 4)

#define __set(_v, _m, _l)  (((_v) << (_l)) & GENMASK(_m, _l))

enum clr_fmt {
	SE_CLR_FMT_INDEX8   = 0x2,
	SE_CLR_FMT_RGB332   = 0x3,
	SE_CLR_FMT_RGB565   = 0x4,
	SE_CLR_FMT_ARGB1555 = 0x5,
	SE_CLR_FMT_ARGB4444 = 0x6,
	SE_CLR_FMT_ARGB8888 = 0x7,
	SE_CLR_FMT_Y        = 0x8,
	SE_CLR_FMT_CBCR     = 0x9,
	SE_CLR_FMT_YCBCR420 = 0xa,
	SE_CLR_FMT_YCBCR422 = 0xb,
	SE_CLR_FMT_RGB888   = 0xf,
};

static inline uint32_t se_reg_clr_fmt_val(enum clr_fmt fmt,
		uint32_t alpha_loc, uint32_t alpha_loc2,
		uint32_t big_endian_i, uint32_t big_endian_o,
		uint32_t rounding_en)
{
	uint32_t val = 0;

	val |= BIT(4) | (fmt & 0xf);
	if (alpha_loc != 0)
		val |= BIT(6) | ((alpha_loc & 1) << 5);
	if (alpha_loc2 != 0)
		val |= BIT(14) | ((alpha_loc2 & 1) << 13);
	val |= BIT(16) | ((big_endian_i & 1) << 15);
	val |= BIT(18) | ((big_endian_i & 1) << 17);
	val |= BIT(20) | ((big_endian_o & 1) << 19);
	val |= BIT(22) | ((rounding_en & 1) << 21);
	return val;
}

static inline uint32_t se_reg_pitch_val(uint32_t pitch, uint32_t interleave)
{
	return __set(pitch, 15, 0) | __set(interleave, 16, 16);
}

struct se_cmd {
	uint32_t cmds[8];
};

enum {
	SE_CMD_OPCODE_WRITE_REG = 0x1,
	SE_CMD_OPCODE_COPY      = 0x9,
	SE_CMD_OPCODE_NOP       = 0xF,
};

static inline void se_cmd_setup_write_reg(struct se_cmd *cmd,
	uint32_t reg_addr, uint32_t value)
{
	cmd->cmds[0] = SE_CMD_OPCODE_WRITE_REG | __set(reg_addr, 15, 4);
	cmd->cmds[1] = value;
	cmd->cmds[2] = SE_CMD_OPCODE_NOP;
	cmd->cmds[3] = SE_CMD_OPCODE_NOP;
	cmd->cmds[4] = SE_CMD_OPCODE_NOP;
	cmd->cmds[5] = SE_CMD_OPCODE_NOP;
	cmd->cmds[6] = SE_CMD_OPCODE_NOP;
	cmd->cmds[7] = SE_CMD_OPCODE_NOP;
}

static inline void se_cmd_setup_copy(struct se_cmd *cmd,
	uint32_t dst_sel, uint32_t dst_x, uint32_t dst_y,
	uint32_t src_sel, uint32_t src_x, uint32_t src_y,
	uint32_t width, uint32_t height, uint32_t swap_en, uint32_t swap_opt)
{
	cmd->cmds[0] = SE_CMD_OPCODE_COPY |
		       __set(width,    15,  4) |
		       __set(height,   27, 16);
	cmd->cmds[1] = __set(dst_x,    11,  0) |
		       __set(dst_y,    23, 12) |
		       __set(dst_sel,  31, 30);
	cmd->cmds[2] = __set(src_x,    11,  0) |
		       __set(src_y,    23, 12) |
		       __set(src_sel,  31, 30) |
		       __set(swap_en,  29, 29) |
		       __set(swap_opt, 28, 24);
	cmd->cmds[3] = SE_CMD_OPCODE_NOP;
	cmd->cmds[4] = SE_CMD_OPCODE_NOP;
	cmd->cmds[5] = SE_CMD_OPCODE_NOP;
	cmd->cmds[6] = SE_CMD_OPCODE_NOP;
	cmd->cmds[7] = SE_CMD_OPCODE_NOP;
}

struct se_device;
struct se_instance;

struct se_cmdq {
	struct se_instance  *ins;
	dma_addr_t          dma;
	void                *virt;
	size_t              size;
	int                 pos;
};

struct se_instance {
	struct se_device    *core;
	int                 id;

	struct se_cmdq      cmdq;

	wait_queue_head_t   cmd_done_wait;
	bool                cmd_done;
	uint32_t            status;
	struct mutex        lock;
};

int se_cmdq_add_cmd(struct se_cmdq *cmdq, struct se_cmd *cmd);
void se_cmdq_reset(struct se_cmdq *cmdq);

int se_cmdq_add_memcpy(struct se_cmdq *cmdq, dma_addr_t dst, dma_addr_t src, uint32_t size);
int se_cmdq_add_memcpy_2d(struct se_cmdq *cmdq, dma_addr_t dst, dma_addr_t src,
		uint32_t dst_stride, uint32_t src_stride, uint32_t width, uint32_t height);


#endif
