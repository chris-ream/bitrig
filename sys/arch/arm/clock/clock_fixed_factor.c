/*
 * Copyright (c) 2014 Patrick Wildt <patrick@blueri.se>
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
#include <sys/malloc.h>
#include <machine/clock.h>
#include <machine/fdt.h>
#include <arm/clock/clockvar.h>

struct clk_fixed_factor
{
	uint32_t mul;
	uint32_t div;
};

static uint32_t
clk_fixed_factor_get_rate(struct clk *clk)
{
	struct clk_fixed_factor *fix = clk->data;
	return (clk_get_rate_parent(clk) * fix->mul) / fix->div;
}

struct clk *
clk_fixed_factor(char *name, char *parent, uint32_t mul, uint32_t div)
{
	struct clk *clk, *p;
	struct clk_fixed_factor *fix;

	clk = malloc(sizeof(struct clk), M_DEVBUF, M_NOWAIT|M_ZERO);
	if (clk == NULL)
		return NULL;

	p = clk_get(parent);
	if (p == NULL)
		goto err;

	fix = malloc(sizeof(struct clk_fixed_factor), M_DEVBUF, M_NOWAIT|M_ZERO);
	if (fix == NULL)
		goto err;

	fix->mul = mul;
	fix->div = div;
	clk->data = fix;
	clk->get_rate = clk_fixed_factor_get_rate;
	clk->parent = p;

	if (!clk_register(clk, name))
		return clk;

err:
	free(clk, M_DEVBUF, sizeof(struct clk));
	return NULL;
}

void
clk_fdt_fixed_factor(void *node)
{
	char *names = fdt_node_name(node), *pnames;
	uint32_t mult, div;
	void *parent;
	int phandle;

	if (!fdt_node_property_int(node, "clock-div", &div))
		return;

	if (!fdt_node_property_int(node, "clock-mult", &mult))
		return;

	fdt_node_property(node, "clock-output-names", &names);

	if (!fdt_node_property_int(node, "clocks", &phandle))
		return;

	if ((parent = fdt_find_node_by_phandle(NULL, phandle)) == NULL)
		return;

	if (!fdt_node_property(parent, "clock-output-names", &pnames))
		return;

	clk_fixed_factor(names, names, mult, div);
}
