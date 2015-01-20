/*
 * Copyright (c) 2013 Patrick Wildt <patrick@blueri.se>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/queue.h>
#include <sys/malloc.h>
#include <sys/device.h>
#include <sys/evcount.h>
#include <sys/socket.h>
#include <sys/timeout.h>
#include <machine/intr.h>
#include <machine/bus.h>
#include <machine/fdt.h>
#include <armv7/armv7/armv7var.h>
#include <armv7/imx/imxiomuxcvar.h>

/* registers */
#define IOMUXC_GPR1			0x004
#define IOMUXC_GPR8			0x020
#define IOMUXC_GPR12			0x030
#define IOMUXC_GPR13			0x034

#define IOMUXC_MUX_CTL_PAD_EIM_EB2	0x08C
#define IOMUXC_MUX_CTL_PAD_EIM_DATA16	0x090
#define IOMUXC_MUX_CTL_PAD_EIM_DATA17	0x094
#define IOMUXC_MUX_CTL_PAD_EIM_DATA18	0x098
#define IOMUXC_MUX_CTL_PAD_EIM_DATA21	0x0A4
#define IOMUXC_MUX_CTL_PAD_EIM_DATA28	0x0C4

#define IOMUXC_PAD_CTL_PAD_EIM_EB2	0x3A0
#define IOMUXC_PAD_CTL_PAD_EIM_DATA16	0x3A4
#define IOMUXC_PAD_CTL_PAD_EIM_DATA17	0x3A8
#define IOMUXC_PAD_CTL_PAD_EIM_DATA18	0x3AC
#define IOMUXC_PAD_CTL_PAD_EIM_DATA21	0x3B8
#define IOMUXC_PAD_CTL_PAD_EIM_DATA28	0x3D8

#define IOMUXC_I2C1_SCL_IN_SELECT_INPUT	0x898
#define IOMUXC_I2C1_SDA_IN_SELECT_INPUT	0x89C
#define IOMUXC_I2C2_SCL_IN_SELECT_INPUT	0x8A0
#define IOMUXC_I2C2_SDA_IN_SELECT_INPUT	0x8A4
#define IOMUXC_I2C3_SCL_IN_SELECT_INPUT	0x8A8
#define IOMUXC_I2C3_SDA_IN_SELECT_INPUT	0x8AC

/* bits and bytes */
#define IOMUXC_GPR1_REF_SSP_EN					(1 << 16)
#define IOMUXC_GPR1_TEST_POWERDOWN				(1 << 18)

#define IOMUXC_GPR8_PCS_TX_DEEMPH_GEN1				(0 << 0)
#define IOMUXC_GPR8_PCS_TX_DEEMPH_GEN2_3P5DB			(0 << 6)
#define IOMUXC_GPR8_PCS_TX_DEEMPH_GEN2_6DB			(20 << 12)
#define IOMUXC_GPR8_PCS_TX_SWING_FULL				(127 << 18)
#define IOMUXC_GPR8_PCS_TX_SWING_LOW				(127 << 25)

#define IOMUXC_GPR12_LOS_LEVEL_MASK				(0x1f << 4)
#define IOMUXC_GPR12_LOS_LEVEL_9				(9 << 4)
#define IOMUXC_GPR12_APPS_PM_XMT_PME				(1 << 9)
#define IOMUXC_GPR12_APPS_LTSSM_ENABLE				(1 << 10)
#define IOMUXC_GPR12_APPS_INIT_RST				(1 << 11)
#define IOMUXC_GPR12_DEVICE_TYPE_RC				(4 << 12)
#define IOMUXC_GPR12_DEVICE_TYPE_MASK				(0xf << 12)
#define IOMUXC_GPR12_APPS_PM_XMT_TURNOFF			(1 << 16)

#define IOMUXC_GPR13_SATA_PHY_1_FAST_EDGE_RATE			(0x00 << 0)
#define IOMUXC_GPR13_SATA_PHY_1_SLOW_EDGE_RATE			(0x02 << 0)
#define IOMUXC_GPR13_SATA_PHY_1_EDGE_RATE_MASK			0x3
#define IOMUXC_GPR13_SATA_PHY_2_1104V				(0x11 << 2)
#define IOMUXC_GPR13_SATA_PHY_3_333DB				(0x00 << 7)
#define IOMUXC_GPR13_SATA_PHY_4_9_16				(0x04 << 11)
#define IOMUXC_GPR13_SATA_PHY_5_SS				(0x01 << 14)
#define IOMUXC_GPR13_SATA_SPEED_3G				(0x01 << 15)
#define IOMUXC_GPR13_SATA_PHY_6					(0x03 << 16)
#define IOMUXC_GPR13_SATA_PHY_7_SATA2M				(0x12 << 19)
#define IOMUXC_GPR13_SATA_PHY_8_30DB				(0x05 << 24)
#define IOMUXC_GPR13_SATA_MASK					0x07FFFFFD

#define IOMUXC_PAD_CTL_SRE_SLOW		(1 << 0)
#define IOMUXC_PAD_CTL_SRE_FAST		(1 << 0)
#define IOMUXC_PAD_CTL_DSE_HIZ		(0 << 3)
#define IOMUXC_PAD_CTL_DSE_240OHM	(1 << 3)
#define IOMUXC_PAD_CTL_DSE_120OHM	(2 << 3)
#define IOMUXC_PAD_CTL_DSE_80OHM	(3 << 3)
#define IOMUXC_PAD_CTL_DSE_60OHM	(4 << 3)
#define IOMUXC_PAD_CTL_DSE_48OHM	(5 << 3)
#define IOMUXC_PAD_CTL_DSE_40OHM	(6 << 3)
#define IOMUXC_PAD_CTL_DSE_34OHM	(7 << 3)
#define IOMUXC_PAD_CTL_SPEED_TBD	(0 << 6)
#define IOMUXC_PAD_CTL_SPEED_LOW	(1 << 6)
#define IOMUXC_PAD_CTL_SPEED_MED	(2 << 6)
#define IOMUXC_PAD_CTL_SPEED_MAX	(3 << 6)
#define IOMUXC_PAD_CTL_ODE_DISABLED	(0 << 11)
#define IOMUXC_PAD_CTL_ODE_ENABLED	(1 << 11)
#define IOMUXC_PAD_CTL_PKE_DISABLED	(0 << 12)
#define IOMUXC_PAD_CTL_PKE_ENABLED	(1 << 12)
#define IOMUXC_PAD_CTL_PUE_KEEP		(0 << 13)
#define IOMUXC_PAD_CTL_PUE_PULL		(1 << 13)
#define IOMUXC_PAD_CTL_PUS_100K_OHM_PD	(0 << 14)
#define IOMUXC_PAD_CTL_PUS_47K_OHM_PU	(1 << 14)
#define IOMUXC_PAD_CTL_PUS_100K_OHM_PU	(2 << 14)
#define IOMUXC_PAD_CTL_PUS_22K_OHM_PU	(3 << 14)
#define IOMUXC_PAD_CTL_HYS_DISABLED	(0 << 16)
#define IOMUXC_PAD_CTL_HYS_ENABLED	(1 << 16)

#define IOMUXC_IMX6Q_I2C_PAD_CTRL	(IOMUXC_PAD_CTL_SRE_FAST | IOMUXC_PAD_CTL_ODE_ENABLED | \
		IOMUXC_PAD_CTL_PKE_ENABLED | IOMUXC_PAD_CTL_PUE_PULL | IOMUXC_PAD_CTL_DSE_40OHM | \
		IOMUXC_PAD_CTL_PUS_100K_OHM_PU | IOMUXC_PAD_CTL_HYS_ENABLED | IOMUXC_PAD_CTL_SPEED_MED)

#define IOMUX_CONFIG_SION		(1 << 4)
#define IOMUX_NO_PAD_CTL		(1U << 31)
#define IOMUX_PAD_SION			(1 << 30)

#define HDEVNAME(sc)			((sc)->sc_dev.dv_xname)
#define HREAD4(sc, reg)							\
	(bus_space_read_4((sc)->sc_iot, (sc)->sc_ioh, (reg)))
#define HWRITE4(sc, reg, val)						\
	bus_space_write_4((sc)->sc_iot, (sc)->sc_ioh, (reg), (val))
#define HSET4(sc, reg, bits)						\
	HWRITE4((sc), (reg), HREAD4((sc), (reg)) | (bits))
#define HCLR4(sc, reg, bits)						\
	HWRITE4((sc), (reg), HREAD4((sc), (reg)) & ~(bits))

struct imx_pin_group {
	uint32_t		mux_reg;
	uint32_t		conf_reg;
	uint32_t		input_reg;
	uint32_t		mux_val;
	uint32_t		input_val;
	uint32_t		conf_val;
};

#define HREAD4(sc, reg)							\
	(bus_space_read_4((sc)->sc_iot, (sc)->sc_ioh, (reg)))
#define HWRITE4(sc, reg, val)						\
	bus_space_write_4((sc)->sc_iot, (sc)->sc_ioh, (reg), (val))
#define HSET4(sc, reg, bits)						\
	HWRITE4((sc), (reg), HREAD4((sc), (reg)) | (bits))
#define HCLR4(sc, reg, bits)						\
	HWRITE4((sc), (reg), HREAD4((sc), (reg)) & ~(bits))

struct imxiomuxc_softc {
	struct device		sc_dev;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;
};

struct imxiomuxc_softc *imxiomuxc_sc;

int imxiomuxc_match(struct device *parent, void *v, void *aux);
void imxiomuxc_attach(struct device *parent, struct device *self, void *args);
void imxiomuxc_fdt(struct imxiomuxc_softc *);
void imxiomuxc_configure_group(struct imxiomuxc_softc *, struct imx_pin_group *);

struct cfattach imxiomuxc_ca = {
	sizeof (struct imxiomuxc_softc), NULL, imxiomuxc_attach
};
struct cfattach imxiomuxc_fdt_ca = {
	sizeof (struct imxiomuxc_softc), imxiomuxc_match, imxiomuxc_attach
};

struct cfdriver imxiomuxc_cd = {
	NULL, "imxiomuxc", DV_DULL
};

int
imxiomuxc_match(struct device *parent, void *v, void *aux)
{
	struct armv7_attach_args *aa = aux;

	if (fdt_node_compatible("fsl,imx6q-iomuxc", aa->aa_node) ||
	    fdt_node_compatible("fsl,imx6dl-iomuxc", aa->aa_node))
		return 1;

	return 0;
}

void
imxiomuxc_attach(struct device *parent, struct device *self, void *args)
{
	struct armv7_attach_args *aa = args;
	struct imxiomuxc_softc *sc = (struct imxiomuxc_softc *) self;
	struct fdt_memory mem;

	sc->sc_iot = aa->aa_iot;
	if (aa->aa_node) {
		if (fdt_get_memory_address(aa->aa_node, 0, &mem))
			panic("%s: could not extract memory data from FDT",
			    __func__);
	} else {
		mem.addr = aa->aa_dev->mem[0].addr;
		mem.size = aa->aa_dev->mem[0].size;
	}

	if (bus_space_map(sc->sc_iot, mem.addr, mem.size, 0, &sc->sc_ioh))
		panic("%s: bus_space_map failed!", __func__);

	printf("\n");
	imxiomuxc_sc = sc;

	imxiomuxc_fdt(sc);

}

void
imxiomuxc_fdt(struct imxiomuxc_softc *sc)
{
	int				 i, ngroup;
	void				*node;
	struct imx_pin_group		*groups, *group;

	node = fdt_find_compatible("fsl,iomuxc");
	if (node == NULL)
		return;

	for (node = fdt_child_node(node);
	     node != NULL;
	     node = fdt_next_node(node)) {
		ngroup = fdt_node_property(node, "fsl,pins", NULL)
		    / sizeof(struct imx_pin_group);
		if (ngroup <= 0)
			continue;

		group = groups = (struct imx_pin_group *)malloc(ngroup *
		    sizeof(struct imx_pin_group), M_DEVBUF, M_WAITOK);
		if (groups == NULL)
			return;

		fdt_node_property_ints(node, "fsl,pins", (uint32_t *)groups,
		    (ngroup * sizeof(struct imx_pin_group)) / sizeof(uint32_t));

		for (i = 0; i < ngroup; i++) {
			if (group->conf_val & IOMUX_PAD_SION)
				group->mux_val |= IOMUX_CONFIG_SION;
			if (group->conf_val & IOMUX_NO_PAD_CTL)
				group->conf_reg = 0;
			imxiomuxc_configure_group(sc, group);
			group++;
		}

		free(groups, M_DEVBUF, ngroup * sizeof(struct imx_pin_group));
	}
}

void
imxiomuxc_configure_group(struct imxiomuxc_softc *sc, struct imx_pin_group *g)
{
	if (g->mux_reg)
		HWRITE4(sc, g->mux_reg, g->mux_val);
	if (g->conf_reg)
		HWRITE4(sc, g->conf_reg, g->conf_val);
	if (g->input_reg)
		HWRITE4(sc, g->input_reg, g->input_val);
}

void
imxiomuxc_enable_sata(void)
{
	struct imxiomuxc_softc *sc = imxiomuxc_sc;

	HWRITE4(sc, IOMUXC_GPR13,
	    (HREAD4(sc, IOMUXC_GPR13) & ~IOMUXC_GPR13_SATA_MASK) |
		IOMUXC_GPR13_SATA_PHY_1_FAST_EDGE_RATE |
		IOMUXC_GPR13_SATA_PHY_2_1104V |
		IOMUXC_GPR13_SATA_PHY_3_333DB |
		IOMUXC_GPR13_SATA_PHY_4_9_16 |
		IOMUXC_GPR13_SATA_SPEED_3G |
		IOMUXC_GPR13_SATA_PHY_6 |
		IOMUXC_GPR13_SATA_PHY_7_SATA2M |
		IOMUXC_GPR13_SATA_PHY_8_30DB);

	HWRITE4(sc, IOMUXC_GPR13,
	    (HREAD4(sc, IOMUXC_GPR13) & ~IOMUXC_GPR13_SATA_PHY_1_SLOW_EDGE_RATE) |
		IOMUXC_GPR13_SATA_PHY_1_SLOW_EDGE_RATE);
}

void
imxiomuxc_enable_pcie(void)
{
	struct imxiomuxc_softc *sc = imxiomuxc_sc;

	imxiomuxc_pcie_ltssm(0);

	HWRITE4(sc, IOMUXC_GPR12,
	    (HREAD4(sc, IOMUXC_GPR12) & ~IOMUXC_GPR12_DEVICE_TYPE_MASK) |
		IOMUXC_GPR12_DEVICE_TYPE_RC);
	HWRITE4(sc, IOMUXC_GPR12,
	    (HREAD4(sc, IOMUXC_GPR12) & ~IOMUXC_GPR12_LOS_LEVEL_MASK) |
		IOMUXC_GPR12_LOS_LEVEL_9);

	HWRITE4(sc, IOMUXC_GPR8,
		IOMUXC_GPR8_PCS_TX_DEEMPH_GEN1 |
		IOMUXC_GPR8_PCS_TX_DEEMPH_GEN2_3P5DB |
		IOMUXC_GPR8_PCS_TX_DEEMPH_GEN2_6DB |
		IOMUXC_GPR8_PCS_TX_SWING_FULL |
		IOMUXC_GPR8_PCS_TX_SWING_LOW);
}

void
imxiomuxc_pcie_refclk(int enable)
{
	struct imxiomuxc_softc *sc = imxiomuxc_sc;

	if (enable)
		HSET4(sc, IOMUXC_GPR1, IOMUXC_GPR1_REF_SSP_EN);
	else
		HCLR4(sc, IOMUXC_GPR1, IOMUXC_GPR1_REF_SSP_EN);
}

int
imxiomuxc_pcie_get_refclk(void)
{
	struct imxiomuxc_softc *sc = imxiomuxc_sc;

	return !((HREAD4(sc, IOMUXC_GPR1) & IOMUXC_GPR1_REF_SSP_EN) == 0);
}

void
imxiomuxc_pcie_test_powerdown(int enable)
{
	struct imxiomuxc_softc *sc = imxiomuxc_sc;

	if (enable)
		HSET4(sc, IOMUXC_GPR1, IOMUXC_GPR1_TEST_POWERDOWN);
	else
		HCLR4(sc, IOMUXC_GPR1, IOMUXC_GPR1_TEST_POWERDOWN);
}

int
imxiomuxc_pcie_get_test_powerdown(void)
{
	struct imxiomuxc_softc *sc = imxiomuxc_sc;

	return !((HREAD4(sc, IOMUXC_GPR1) & IOMUXC_GPR1_TEST_POWERDOWN) == 0);
}

void
imxiomuxc_pcie_ltssm(int enable)
{
	struct imxiomuxc_softc *sc = imxiomuxc_sc;

	if (enable)
		HSET4(sc, IOMUXC_GPR12, IOMUXC_GPR12_APPS_LTSSM_ENABLE);
	else
		HCLR4(sc, IOMUXC_GPR12, IOMUXC_GPR12_APPS_LTSSM_ENABLE);
}

int
imxiomuxc_pcie_get_ltssm(void)
{
	struct imxiomuxc_softc *sc = imxiomuxc_sc;

	return !((HREAD4(sc, IOMUXC_GPR12) & IOMUXC_GPR12_APPS_LTSSM_ENABLE) == 0);
}


void
imxiomuxc_enable_i2c(int x)
{
	struct imxiomuxc_softc *sc = imxiomuxc_sc;

	/* let's just use EIM for those */
	switch (x) {
		case 0:
			/* scl in select */
			HWRITE4(sc, IOMUXC_MUX_CTL_PAD_EIM_DATA21, IOMUX_CONFIG_SION | 6);
			HWRITE4(sc, IOMUXC_I2C1_SCL_IN_SELECT_INPUT, 0);
			HWRITE4(sc, IOMUXC_PAD_CTL_PAD_EIM_DATA21, IOMUXC_IMX6Q_I2C_PAD_CTRL);
			/* sda in select */
			HWRITE4(sc, IOMUXC_MUX_CTL_PAD_EIM_DATA28, IOMUX_CONFIG_SION | 1);
			HWRITE4(sc, IOMUXC_I2C1_SDA_IN_SELECT_INPUT, 0);
			HWRITE4(sc, IOMUXC_PAD_CTL_PAD_EIM_DATA28, IOMUXC_IMX6Q_I2C_PAD_CTRL);
			break;
		case 1:
			/* scl in select */
			HWRITE4(sc, IOMUXC_MUX_CTL_PAD_EIM_EB2, IOMUX_CONFIG_SION | 6);
			HWRITE4(sc, IOMUXC_I2C2_SCL_IN_SELECT_INPUT, 0);
			HWRITE4(sc, IOMUXC_PAD_CTL_PAD_EIM_EB2, IOMUXC_IMX6Q_I2C_PAD_CTRL);
			/* sda in select */
			HWRITE4(sc, IOMUXC_MUX_CTL_PAD_EIM_DATA16, IOMUX_CONFIG_SION | 6);
			HWRITE4(sc, IOMUXC_I2C2_SDA_IN_SELECT_INPUT, 0);
			HWRITE4(sc, IOMUXC_PAD_CTL_PAD_EIM_DATA16, IOMUXC_IMX6Q_I2C_PAD_CTRL);
			break;
		case 2:
			/* scl in select */
			HWRITE4(sc, IOMUXC_MUX_CTL_PAD_EIM_DATA17, IOMUX_CONFIG_SION | 6);
			HWRITE4(sc, IOMUXC_I2C3_SCL_IN_SELECT_INPUT, 0);
			HWRITE4(sc, IOMUXC_PAD_CTL_PAD_EIM_DATA17, IOMUXC_IMX6Q_I2C_PAD_CTRL);
			/* sda in select */
			HWRITE4(sc, IOMUXC_MUX_CTL_PAD_EIM_DATA18, IOMUX_CONFIG_SION | 6);
			HWRITE4(sc, IOMUXC_I2C3_SDA_IN_SELECT_INPUT, 0);
			HWRITE4(sc, IOMUXC_PAD_CTL_PAD_EIM_DATA18, IOMUXC_IMX6Q_I2C_PAD_CTRL);
			break;
	}
}
