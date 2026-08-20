
#include <linux/clk-provider.h>
#include <linux/clk.h>
#include <linux/spinlock.h>
#include <linux/delay.h>
#include "common.h"
#include "clk-pll.h"

#define PLL_EDP1	0x0
#define PLL_EDP2	0x4

/* PLL_EDP1 */
#define PLL_POWON	(0x1 << 31)
#define PLL_LDO_POWON	(0x1 << 27)
#define PLL_RS_SHIFT	(16)
#define PLL_RS_MASK	(0x7 << PLL_RS_SHIFT)
#define PLL_IP_SHIFT	(12)
#define PLL_IP_MASK	(0x7 << PLL_IP_SHIFT)
#define PLL_BIAS_POW	(0x1)

/* PLL_EDP2 */
#define PLL_EN		(0x1)
#define PLL_RST		(0x1 << 1)

#define SSC_EDP0	0x0
#define SSC_EDP1	0x4
#define SSC_EDP2	0x8
#define SSC_EDP3	0xc
#define SSC_EDP4	0x10
#define SSC_EDP5	0x14

/* SSC_EDP0 */
#define SSC_RST		(0x1 << 2)
#define OC_EN		(0x1)

static int clk_pll_edp_enable(struct clk_hw *hw)
{
	struct clk_pll_edp *pll = to_clk_pll_edp(hw);

	clk_regmap_update(&pll->clkr, pll->pll_ofs,
			  PLL_POWON | PLL_LDO_POWON | PLL_BIAS_POW |
			  PLL_RS_MASK | PLL_IP_MASK,
			  PLL_POWON | PLL_LDO_POWON | PLL_BIAS_POW |
			  (5 << PLL_RS_SHIFT) | (6 << PLL_IP_SHIFT));
	udelay(100);
	clk_regmap_update(&pll->clkr, pll->pll_ofs + PLL_EDP2,
			  PLL_EN | PLL_RST, PLL_EN | PLL_RST);
	udelay(100);

	clk_regmap_update(&pll->clkr, pll->ssc_ofs, SSC_RST, SSC_RST);
	clk_regmap_write(&pll->clkr, pll->ssc_ofs + SSC_EDP1, 0x30800);
	clk_regmap_write(&pll->clkr, pll->ssc_ofs + SSC_EDP2, 0x501068);
	clk_regmap_write(&pll->clkr, pll->ssc_ofs + SSC_EDP3, 0x30480);
	clk_regmap_write(&pll->clkr, pll->ssc_ofs + SSC_EDP4, 0x1100);
	clk_regmap_update(&pll->clkr, pll->ssc_ofs,
			  SSC_RST | OC_EN, SSC_RST | OC_EN);

	return 0;
}

static void clk_pll_edp_disable(struct clk_hw *hw)
{

}

static int clk_pll_edp_is_enabled(struct clk_hw *hw)
{
	return 0;
}

static void clk_pll_edp_disable_unused(struct clk_hw *hw)
{

}

const struct clk_ops clk_pll_edp_ops = {
	.enable           = clk_pll_edp_enable,
	.disable          = clk_pll_edp_disable,
	.disable_unused   = clk_pll_edp_disable_unused,
	.is_enabled       = clk_pll_edp_is_enabled,
};
