
#include <linux/clk-provider.h>
#include <linux/clk.h>
#include <linux/spinlock.h>
#include <linux/delay.h>
#include "common.h"
#include "clk-pll.h"

#define PLL_PIXEL1 	0x0
#define PLL_PIXEL2	0x4

/* PLL_PIXEL1 */
#define PLL_POWON	(0x1 << 31)
#define PLL_CS_SHIFT	(16)
#define PLL_CS_MASK	(0x3 << PLL_CS_SHIFT)
#define PLL_RST		(0x1 << 14)
#define PLL_BPSIN	(0x1 << 11)
#define PLL_BPSPI	(0x1 << 1)

/* PLL_PIXEL2 */
#define PLL_EN		(0x1)

#define SSC_PIXEL0	0x0
#define SSC_PIXEL1	0x4
#define SSC_PIXEL2	0x8

/* SSC_PIXEL0 */
#define SSC_RST		(0x1 << 2)
#define OC_EN		(0x1)

static int clk_pll_pixel_enable(struct clk_hw *hw)
{
	struct clk_pll_pixel *pll = to_clk_pll_pixel(hw);

	clk_regmap_update(&pll->clkr, pll->pll_ofs, PLL_RST, 0);
	udelay(100);
	clk_regmap_update(&pll->clkr, pll->pll_ofs,
			  PLL_POWON | PLL_RST | PLL_CS_MASK |
			  PLL_BPSIN | PLL_BPSPI | 0x10,
			  PLL_POWON | PLL_RST | PLL_CS_MASK | PLL_BPSPI | 0x10);
	udelay(100);
	clk_regmap_update(&pll->clkr, pll->pll_ofs + PLL_PIXEL2,
			  PLL_EN, PLL_EN);
	udelay(100);

	clk_regmap_update(&pll->clkr, pll->ssc_ofs, SSC_RST, SSC_RST);
	clk_regmap_write(&pll->clkr, pll->ssc_ofs + SSC_PIXEL1, 0xF000);
	clk_regmap_write(&pll->clkr, pll->ssc_ofs + SSC_PIXEL2, 0x501008);
	clk_regmap_update(&pll->clkr, pll->ssc_ofs,
			  SSC_RST | OC_EN, SSC_RST | OC_EN);

	return 0;
}

static void clk_pll_pixel_disable(struct clk_hw *hw)
{

}

static int clk_pll_pixel_is_enabled(struct clk_hw *hw)
{
	return 0;
}

static void clk_pll_pixel_disable_unused(struct clk_hw *hw)
{

}

static long clk_pll_pixel_round_rate(struct clk_hw *hw,
				     unsigned long rate,
				     unsigned long *parent_rate)
{
	return 0;
}

static int clk_pll_pixel_set_rate(struct clk_hw *hw,
				  unsigned long rate,
				  unsigned long parent_rate)
{
	return 0;
}

static unsigned long clk_pll_pixel_recalc_rate(struct clk_hw *hw,
					       unsigned long parent_rate)
{
	return 0;
}

const struct clk_ops clk_pll_pixel_ops = {
	.enable           = clk_pll_pixel_enable,
	.disable          = clk_pll_pixel_disable,
	.disable_unused   = clk_pll_pixel_disable_unused,
	.is_enabled       = clk_pll_pixel_is_enabled,
	.set_rate         = clk_pll_pixel_set_rate,
	.round_rate       = clk_pll_pixel_round_rate,
	.recalc_rate      = clk_pll_pixel_recalc_rate,
};
