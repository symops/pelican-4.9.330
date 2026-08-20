/*
 * Copyright (C) 2021 Realtek Ltd.
 * Author: PK Chuang <pk.chuang@realtek.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/dma-mapping.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <linux/mtd/spi-nor.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <soc/realtek/rtd129x_lockapi.h>

#define SFC_OPCODE		(0x00)
#define SFC_CTL			(0x04)
#define SFC_SCK			(0x08)
#define SFC_CE			(0x0c)
#define SFC_WP			(0x10)
#define SFC_POS_LATCH		(0x14)
#define SFC_WAIT_WR		(0x18)
#define SFC_EN_WR		(0x1c)
#define SFC_FAST_RD		(0x20)
#define SFC_SCK_TAP		(0x24)
#define SFP_OPCODE2		(0x28)

#ifdef CONFIG_ARCH_RTD13xx
#define MD_BASE_ADDE            0x9801b700
#define MD_FDMA_DDR_SADDR	(0xc)
#define MD_FDMA_FL_SADDR	(0x10)
#define MD_FDMA_CTRL2		(0x14)
#define MD_FDMA_CTRL1		(0x18)
#elif CONFIG_ARCH_RTD129x
#define MD_BASE_ADDE            0x9800b000
#define MD_FDMA_DDR_SADDR	(0x88)
#define MD_FDMA_FL_SADDR	(0x8c)
#define MD_FDMA_CTRL2		(0x90)
#define MD_FDMA_CTRL1		(0x94)
#endif

#define RTKSFC_DMA_MAX_LEN	0x100
#define RTKSFC_MAX_CHIP_NUM	1
#define RTKSFC_WAIT_TIMEOUT	1000000	
#define RTKSFC_OP_READ          0x0
#define RTKSFC_OP_WRITE         0x1

#define NOR_BASE_PHYS           0x88100000

//#define RTK_USERDEFINE_PARTITION 1
#ifdef RTK_USERDEFINE_PARTITION
static struct mtd_partition rtk_partitions[] =
{
	{
		.name   = "rtknor",
		.size   = 0x800000,
		.offset = 0x800000
	}
};
#endif

struct rtksfc_priv {
	u32 chipselect;
	u32 clkrate;
	struct rtksfc_host *host;
};

struct rtksfc_host {
	struct device *dev;
	struct mutex lock;
	void __iomem *regbase;
	void __iomem *iobase;
	void __iomem *mdbase;
	struct clk *clk;
	void *buffer;
	dma_addr_t dma_buffer;
	struct spi_nor	*nor[RTKSFC_MAX_CHIP_NUM];
	u32 num_chip;
	unsigned long lock_flag;
};

#define SFC_SYNC \
	do { \
		void __iomem *r; \
		r = ioremap(0x9801a020, 0x4); \
		asm volatile("DMB SY" : : : "memory"); \
		writel(0x0, r); \
		asm volatile("DMB SY" : : : "memory"); \
		iounmap(r); \
	} while(0);

static int rtk_spi_nor_read_status(struct rtksfc_host *host)
{
	int i = 100;

	while (i--) {
		rtk_lockapi_lock(host->lock_flag, (char *)__FUNCTION__);

		writel(0x05, host->regbase + SFC_OPCODE);
		udelay(50);
		writel(0x10, host->regbase + SFC_CTL);
		SFC_SYNC

		rtk_lockapi_unlock(host->lock_flag, (char *)__FUNCTION__);

		if ((*(volatile unsigned char *)host->iobase) & 0x1)
			msleep(100);
		else 
			return 0;
	}

	return -1;
}

static void rtk_spi_nor_read_mode(struct rtksfc_host *host)
{
	unsigned int tmp;

	rtk_lockapi_lock(host->lock_flag, (char *)__FUNCTION__);

	writel(0x03, host->regbase + SFC_OPCODE);
	udelay(50);
	writel(0x18, host->regbase + SFC_CTL);
	SFC_SYNC
	tmp = *(volatile unsigned int *)(host->iobase);
	
	rtk_lockapi_unlock(host->lock_flag, (char *)__FUNCTION__);

	return;
}

static void rtk_spi_nor_write_mode(struct rtksfc_host *host)
{
	rtk_lockapi_lock(host->lock_flag, (char *)__FUNCTION__);

	writel(0x2, host->regbase + SFC_OPCODE);
	udelay(50);
	writel(0x18, host->regbase + SFC_CTL);
	SFC_SYNC

	rtk_lockapi_unlock(host->lock_flag, (char *)__FUNCTION__);

	return;
}

static void rtk_spi_nor_enable_auto_write(struct rtksfc_host *host)
{
	writel(0x105, host->regbase + SFC_WAIT_WR);
        writel(0x106, host->regbase + SFC_EN_WR);
	
	writel(0x001A1307, host->regbase + SFC_CE);

	return;
}

static void rtk_spi_nor_disable_auto_write(struct rtksfc_host *host)
{
	rtk_lockapi_lock(host->lock_flag, (char *)__FUNCTION__);

	writel(0x005, host->regbase + SFC_WAIT_WR);
        writel(0x006, host->regbase + SFC_EN_WR);

	rtk_lockapi_unlock(host->lock_flag, (char *)__FUNCTION__);

	return;
}

static void rtk_spi_nor_init_setting(void)
{
	void __iomem *regbase;

#ifdef CONFIG_ARCH_RTD13xx
	regbase = ioremap(0x9804e000, 0x4);
	writel((readl(regbase) & 0x00ffffff) | 0xaa000000, regbase);
	iounmap(regbase);

	regbase = ioremap(0x9804e120, 0x4);
	writel((readl(regbase) & 0xfffff7ff) | 0x00000800, regbase);
	iounmap(regbase);

	regbase = ioremap(0x98000054, 0x4);
	writel(readl(regbase) | (0x3 << 4), regbase);
	iounmap(regbase);

	regbase = ioremap(0x98000008, 0x4);
	writel(readl(regbase) | (0x3 << 4), regbase);
	iounmap(regbase);
#elif CONFIG_ARCH_RTD129x
	regbase = ioremap(0x9801a914, 0x4);
	writel(readl(regbase) | 0x00000001, regbase);
	iounmap(regbase);
#endif
}

static void rtk_spi_nor_init(struct rtksfc_host *host)
{
	rtk_spi_nor_init_setting();

	host->iobase = ioremap(NOR_BASE_PHYS, 0x2000000);
	host->mdbase = ioremap(MD_BASE_ADDE, 0x100);

	writel(0x00000013, host->regbase + SFC_SCK);
	writel(0x001a1307, host->regbase + SFC_CE);
	writel(0x00000000, host->regbase + SFC_POS_LATCH);
	writel(0x00000005, host->regbase + SFC_WAIT_WR);
	writel(0x00000006, host->regbase + SFC_EN_WR);

	return;	
}

static int rtk_spi_nor_prep(struct spi_nor *nor, enum spi_nor_ops ops)
{
	struct rtksfc_priv *priv = nor->priv;
	struct rtksfc_host *host = priv->host;

	switch (ops) {
	case SPI_NOR_OPS_READ:
		rtk_spi_nor_disable_auto_write(host);
		break;

	case SPI_NOR_OPS_WRITE:
		rtk_spi_nor_enable_auto_write(host);
		break;

	case SPI_NOR_OPS_ERASE:
		rtk_spi_nor_disable_auto_write(host);
		break;

	default:
		break;
	}
	
	return 0;
}

static void rtk_spi_nor_unprep(struct spi_nor *nor, enum spi_nor_ops ops)
{
	switch (ops) {
	case SPI_NOR_OPS_READ:
		break;

	case SPI_NOR_OPS_WRITE:
		break;

	case SPI_NOR_OPS_ERASE:
		break;

	default:
		break;
	}
	
	return;
}

static int rtk_spi_nor_read_reg(struct spi_nor *nor, u8 opcode, u8 *buf,
				int len)
{
	struct rtksfc_priv *priv = nor->priv;
        struct rtksfc_host *host = priv->host;
	unsigned int value;
	unsigned char val;

	rtk_lockapi_lock(host->lock_flag, (char *)__FUNCTION__);

	writel(opcode, host->regbase + SFC_OPCODE);
	udelay(50);

	switch (opcode) {
	case SPINOR_OP_RDID:
		writel(0x00000010, host->regbase + SFC_CTL);
		SFC_SYNC
		value = *(volatile unsigned int *)host->iobase;
		memcpy(buf, (unsigned char *)host->iobase, len);
		break;		

	case SPINOR_OP_RDSR:
		writel(0x00000010, host->regbase + SFC_CTL);
		SFC_SYNC
		val = *(volatile unsigned char *)host->iobase;
		memcpy(buf, (unsigned char *)host->iobase, len);
                break;

        default:
                dev_warn(nor->dev, "rtk_spi_nor_read_reg, unknow command\n");
                break;
        }

	rtk_lockapi_unlock(host->lock_flag, (char *)__FUNCTION__);

	return 0;
}

static int rtk_spi_nor_write_reg(struct spi_nor *nor, u8 opcode,
				u8 *buf, int len)
{
	struct rtksfc_priv *priv = nor->priv;
	struct rtksfc_host *host = priv->host;
	unsigned int tmp;

	rtk_lockapi_lock(host->lock_flag, (char *)__FUNCTION__);

	writel(opcode, host->regbase + SFC_OPCODE);
	udelay(50);

	switch (opcode) {
	case SPINOR_OP_WRSR:
		writel(0x10, host->regbase + SFC_CTL);
		SFC_SYNC
		*(volatile unsigned char *)(host->iobase) = buf[0];
		break;	
	
        case SPINOR_OP_WREN:
		writel(0x0, host->regbase + SFC_CTL);
		SFC_SYNC
		tmp = *(volatile unsigned char *)(host->iobase);	
		break;

        case SPINOR_OP_WRDI:
                writel(0x0, host->regbase + SFC_CTL);
		SFC_SYNC
		break;

        case SPINOR_OP_EN4B:
                writel(0x0, host->regbase + SFC_CTL);
		SFC_SYNC
                tmp = *(volatile unsigned int *)(host->iobase);
		
		/* controller setting */
		writel(0x1, host->regbase + SFP_OPCODE2);
		break;

        case SPINOR_OP_EX4B:
                writel(0x0, host->regbase + SFC_CTL);
		SFC_SYNC
		tmp = *(volatile unsigned int *)(host->iobase);
		
		/* controller setting */
                writel(0x0, host->regbase + SFP_OPCODE2);
                break;

	case SPINOR_OP_CHIP_ERASE:
		dev_info(nor->dev, "Erase whole flash.\n");
		writel(0x0, host->regbase + SFC_CTL);
		SFC_SYNC
		tmp = *(volatile unsigned char *)host->iobase;

		tmp = rtk_spi_nor_read_status(host);
		break;

        default:
		dev_warn(nor->dev, "rtk_spi_nor_write_reg, unknow command\n");
                break;
        }

	rtk_lockapi_unlock(host->lock_flag, (char *)__FUNCTION__);

	return 0;
}

static int rtk_spi_nor_byte_transfer(struct spi_nor *nor, loff_t offset, 
				size_t len, unsigned char *buf, u8 op_type)
{
	struct rtksfc_priv *priv = nor->priv;
        struct rtksfc_host *host = priv->host;

	if (op_type == RTKSFC_OP_READ)
		memcpy_fromio(buf, (unsigned char *)(host->iobase + offset), len);
	else
		memcpy_toio((unsigned char *)(host->iobase + offset), buf, len);

	return len;
}

static int rtk_spi_nor_dma_transfer(struct spi_nor *nor, loff_t offset,
					size_t len, u8 op_type)
{
	struct rtksfc_priv *priv = nor->priv;
	struct rtksfc_host *host = priv->host;
	unsigned int val;

	writel(0x0a, host->mdbase + MD_FDMA_CTRL1);

	/* setup MD DDR addr and flash addr */
	writel((unsigned long)(host->dma_buffer),  
				host->mdbase + MD_FDMA_DDR_SADDR);
	writel((unsigned long)((volatile u8*)(NOR_BASE_PHYS + offset)),  
				host->mdbase + MD_FDMA_FL_SADDR);

	if (op_type == RTKSFC_OP_READ)
		val = (0x2C000000 | len);
	else
		val = (0x26000000 | len);
	
	writel(val, host->mdbase + MD_FDMA_CTRL2);

	mb();
	SFC_SYNC

	/* go */
	writel(0x03, host->mdbase + MD_FDMA_CTRL1);

	udelay(100);

	while (readl(host->mdbase + MD_FDMA_CTRL1) & 0x1) {
		udelay(100);
	}

	return rtk_spi_nor_read_status(host);
}

static ssize_t rtk_spi_nor_read(struct spi_nor *nor, loff_t from, size_t len,
				u_char *read_buf)
{
	struct rtksfc_priv *priv = nor->priv;
	struct rtksfc_host *host = priv->host;
	loff_t n_from;
	size_t n_len = 0, r_len = 0;
	unsigned int offset;
	int ret;
	int i;

	offset = from & 0x3;

	/* Byte stage */
	if (offset != 0) {
		r_len = (offset > len) ? len : offset;
		rtk_spi_nor_read_mode(host);
		ret = rtk_spi_nor_byte_transfer(nor, from, r_len, (u8 *)read_buf, 
							RTKSFC_OP_READ);
	}

	n_from = from + r_len;
	n_len = len - r_len;

	/* DMA stage */
	while (n_len > 0) {
		r_len = (n_len >= RTKSFC_DMA_MAX_LEN) ? RTKSFC_DMA_MAX_LEN : n_len;

		rtk_spi_nor_read_mode(host);
	
		ret = rtk_spi_nor_dma_transfer(nor, n_from, r_len, 
							RTKSFC_OP_READ);
		if (ret) {
			dev_err(nor->dev, "DMA read timeout\n");
			return ret;
		}

		for (i = 0; i < r_len; i++)
			*(u8 *)(read_buf + offset + i) = *(u8 *)(host->buffer + i);

		n_len -= r_len;
		offset += r_len;
		n_from += r_len;
	}

	return len;
}

static ssize_t rtk_spi_nor_write(struct spi_nor *nor, loff_t to,
				size_t len, const u_char *write_buf)
{
	struct rtksfc_priv *priv = nor->priv;
	struct rtksfc_host *host = priv->host;
	int r_len = (int)len, w_len = 0;
	int offset;
	int ret;
	int i;

	offset = to & 0x3;

	/* byte stage */
	if (offset != 0) {
		w_len = (offset > len) ? len : offset;
		rtk_spi_nor_write_mode(host);
		ret = rtk_spi_nor_byte_transfer(nor, to, w_len, (u8 *)write_buf, 
							RTKSFC_OP_WRITE);
	}

	to = to + w_len;
	r_len = (int)len - w_len;

	/* DMA stage */
	offset = 0;
	while (r_len > 0) {
		w_len = (r_len >= RTKSFC_DMA_MAX_LEN) ? RTKSFC_DMA_MAX_LEN : r_len;

		for(i = 0; i < w_len; i++)
			*(u8 *)(host->buffer + i) = *(u8 *)(write_buf + i);

		rtk_spi_nor_write_mode(host);

		ret = rtk_spi_nor_dma_transfer(nor, to + offset, w_len, 
							RTKSFC_OP_WRITE);

		r_len = r_len - w_len;
		offset = offset + w_len;
	}

	rtk_spi_nor_read_mode(host);

	return len;
}

static int rtk_spi_nor_erase(struct spi_nor *nor, loff_t offs)
{
	struct rtksfc_priv *priv = nor->priv;
        struct rtksfc_host *host = priv->host;
	unsigned char tmp;
	
	rtk_lockapi_lock(host->lock_flag, (char *)__FUNCTION__);

	writel(nor->erase_opcode, host->regbase + SFC_OPCODE);
	udelay(50);
	writel(0x08, host->regbase + SFC_CTL);	

	tmp = *(volatile unsigned char *)(host->iobase + offs);
	SFC_SYNC

	rtk_lockapi_unlock(host->lock_flag, (char *)__FUNCTION__);

	return rtk_spi_nor_read_status(host);
}

static int rtk_spi_nor_register(struct device_node *np,
				struct rtksfc_host *host)
{
	struct device *dev = host->dev;
	struct spi_nor *nor;
	struct rtksfc_priv *priv;
	struct mtd_info *mtd;
	int ret;

	nor = devm_kzalloc(dev, sizeof(*nor), GFP_KERNEL);
	if (!nor)
		return -ENOMEM;

	nor->dev = dev;
	spi_nor_set_flash_node(nor, np);

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->host = host;
	nor->priv = priv;

	nor->prepare = rtk_spi_nor_prep;
	nor->unprepare = rtk_spi_nor_unprep;
	nor->read_reg = rtk_spi_nor_read_reg;
	nor->write_reg = rtk_spi_nor_write_reg;
	nor->read = rtk_spi_nor_read;
	nor->write = rtk_spi_nor_write;
	nor->erase = rtk_spi_nor_erase;

	ret = spi_nor_scan(nor, NULL, SPI_NOR_FAST);
	if (ret) 
		return ret;

	mtd = &nor->mtd;
	mtd->writesize = nor->page_size;

#ifdef RTK_USERDEFINE_PARTITION
	ret = mtd_device_register(mtd, rtk_partitions, ARRAY_SIZE(rtk_partitions));
#else
	ret = mtd_device_register(mtd, NULL, 0);
#endif
	if (ret)
		return ret;

	host->nor[host->num_chip] = nor;
	host->num_chip++;

	return 0;
}

static void rtk_spi_nor_unregister_all(struct rtksfc_host *host)
{
	int i;

	for (i = 0; i < host->num_chip; i++)
		mtd_device_unregister(&host->nor[i]->mtd);
}

static int rtk_spi_nor_register_all(struct rtksfc_host *host)
{
	struct device_node *np = NULL;
	int ret;

	ret = rtk_spi_nor_register(np, host);
	if (ret)
		goto fail;

	return 0;

fail:
	rtk_spi_nor_unregister_all(host);
	return ret;
}

static int rtk_spi_nor_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct resource *res;
	struct rtksfc_host *host;
	int ret;

	host = devm_kzalloc(dev, sizeof(*host), GFP_KERNEL);
	if (!host)
		return -ENOMEM;

	platform_set_drvdata(pdev, host);
	host->dev = dev;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	host->regbase = devm_ioremap_resource(dev, res);
	if (IS_ERR(host->regbase))
		return PTR_ERR(host->regbase);

	ret = dma_set_mask_and_coherent(dev, DMA_BIT_MASK(32));
        if (ret) {
                dev_err(dev, "Unable to set dma mask\n");
                return ret;
        }

	host->buffer = dmam_alloc_coherent(dev, RTKSFC_DMA_MAX_LEN,
			&host->dma_buffer, GFP_KERNEL);
	if (!host->buffer)
		return -ENOMEM;

	mutex_init(&host->lock);

	rtk_spi_nor_init(host);

	ret = rtk_spi_nor_register_all(host);
	if (ret)
		mutex_destroy(&host->lock);

	return ret;
}

static int rtk_spi_nor_remove(struct platform_device *pdev)
{
	struct rtksfc_host *host = platform_get_drvdata(pdev);

	mutex_destroy(&host->lock);

	return 0;
}

static const struct of_device_id rtk_spi_nor_dt_ids[] = {
	{ .compatible = "realtek,rtk-sfc"},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, rtk_spi_nor_dt_ids);

static struct platform_driver rtk_spi_nor_driver = {
	.driver = {
		.name	= "rtk-sfc",
		.of_match_table = rtk_spi_nor_dt_ids,
	},
	.probe	= rtk_spi_nor_probe,
	.remove	= rtk_spi_nor_remove,
};
module_platform_driver(rtk_spi_nor_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Realtek SPI Nor Flash Controller Driver");
