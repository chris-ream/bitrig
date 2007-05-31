/*	$OpenBSD: sdmmc_io.c,v 1.8 2007/05/31 10:09:01 uwe Exp $	*/

/*
 * Copyright (c) 2006 Uwe Stuehler <uwe@openbsd.org>
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

/* Routines for SD I/O cards. */

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/proc.h>
#include <sys/systm.h>

#include <dev/sdmmc/sdmmc_ioreg.h>
#include <dev/sdmmc/sdmmcchip.h>
#include <dev/sdmmc/sdmmcreg.h>
#include <dev/sdmmc/sdmmcvar.h>

struct sdmmc_intr_handler {
	struct sdmmc_softc *ih_softc;
	char *ih_name;
	int (*ih_fun)(void *);
	void *ih_arg;
	TAILQ_ENTRY(sdmmc_intr_handler) entry;
};

int	sdmmc_submatch(struct device *, void *, void *);
int	sdmmc_print(void *, const char *);
int	sdmmc_io_rw_direct(struct sdmmc_softc *, struct sdmmc_function *,
	    int, u_char *, int);
int	sdmmc_io_rw_extended(struct sdmmc_softc *, struct sdmmc_function *,
	    int, u_char *, int, int);
int	sdmmc_io_xchg(struct sdmmc_softc *, struct sdmmc_function *,
	    int, u_char *);
void	sdmmc_io_reset(struct sdmmc_softc *);
int	sdmmc_io_send_op_cond(struct sdmmc_softc *, u_int32_t, u_int32_t *);

#ifdef SDMMC_DEBUG
#define DPRINTF(s)	printf s
#else
#define DPRINTF(s)	/**/
#endif

#ifdef SDMMC_DEBUG
int	sdmmc_verbose = 1;
#else
int	sdmmc_verbose = 0;
#endif

/*
 * Initialize SD I/O card functions (before memory cards).  The host
 * system and controller must support card interrupts in order to use
 * I/O functions.
 */
int
sdmmc_io_enable(struct sdmmc_softc *sc)
{
	u_int32_t host_ocr;
	u_int32_t card_ocr;

	/* Set host mode to SD "combo" card. */
	SET(sc->sc_flags, SMF_SD_MODE|SMF_IO_MODE|SMF_MEM_MODE);

	/* Reset I/O functions. */
	sdmmc_io_reset(sc);

	/*
	 * Read the I/O OCR value, determine the number of I/O
	 * functions and whether memory is also present (a "combo
	 * card") by issuing CMD5.  SD memory-only and MMC cards
	 * do not respond to CMD5.
	 */
	if (sdmmc_io_send_op_cond(sc, 0, &card_ocr) != 0) {
		/* No SDIO card; switch to SD memory-only mode. */
		CLR(sc->sc_flags, SMF_IO_MODE);
		return 0;
	}

	/* Parse the additional bits in the I/O OCR value. */
	if (!ISSET(card_ocr, SD_IO_OCR_MEM_PRESENT)) {
		/* SDIO card without memory (not a "combo card"). */
		DPRINTF(("%s: no memory present\n", SDMMCDEVNAME(sc)));
		CLR(sc->sc_flags, SMF_MEM_MODE);
	}
	sc->sc_function_count = SD_IO_OCR_NUM_FUNCTIONS(card_ocr);
	if (sc->sc_function_count == 0) {
		/* Useless SDIO card without any I/O functions. */
		DPRINTF(("%s: no I/O functions\n", SDMMCDEVNAME(sc)));
		CLR(sc->sc_flags, SMF_IO_MODE);
		return 0;
	}
	card_ocr &= SD_IO_OCR_MASK;

	/* Set the lowest voltage supported by the card and host. */
	host_ocr = sdmmc_chip_host_ocr(sc->sct, sc->sch);
	if (sdmmc_set_bus_power(sc, host_ocr, card_ocr) != 0) {
		printf("%s: can't supply voltage requested by card\n",
		    SDMMCDEVNAME(sc));
		return 1;
	}

	/* Reset I/O functions (again). */
	sdmmc_io_reset(sc);

	/* Send the new OCR value until all cards are ready. */
	if (sdmmc_io_send_op_cond(sc, host_ocr, NULL) != 0) {
		printf("%s: can't send I/O OCR\n", SDMMCDEVNAME(sc));
		return 1;
	}
	return 0;
}

/*
 * Allocate sdmmc_function structures for SD card I/O function
 * (including function 0).
 */
void
sdmmc_io_scan(struct sdmmc_softc *sc)
{
	struct sdmmc_function *sf0, *sf;
	int i;

	sf0 = sdmmc_function_alloc(sc);
	sf0->number = 0;
	if (sdmmc_set_relative_addr(sc, sf0) != 0) {
		printf("%s: can't set I/O RCA\n", SDMMCDEVNAME(sc));
		SET(sf0->flags, SFF_ERROR);
		return;
	}
	sc->sc_fn0 = sf0;
	SIMPLEQ_INSERT_TAIL(&sc->sf_head, sf0, sf_list);

	/* Verify that the RCA has been set by selecting the card. */
	if (sdmmc_select_card(sc, sf0) != 0) {
		printf("%s: can't select I/O RCA %d\n", SDMMCDEVNAME(sc),
		    sf0->rca);
		SET(sf0->flags, SFF_ERROR);
		return;
	}

	for (i = 1; i <= sc->sc_function_count; i++) {
		sf = sdmmc_function_alloc(sc);
		sf->number = i;
		sf->rca = sf0->rca;

		SIMPLEQ_INSERT_TAIL(&sc->sf_head, sf, sf_list);
	}
}

/*
 * Initialize SDIO card functions.
 */
int
sdmmc_io_init(struct sdmmc_softc *sc, struct sdmmc_function *sf)
{
	if (sf->number == 0) {
		sdmmc_io_write_1(sf, SD_IO_CCCR_BUS_WIDTH,
		    CCCR_BUS_WIDTH_1);

		if (sdmmc_read_cis(sf, &sf->cis) != 0) {
			printf("%s: can't read CIS\n", SDMMCDEVNAME(sc));
			SET(sf->flags, SFF_ERROR);
			return 1;
		}

		sdmmc_check_cis_quirks(sf);

		if (sdmmc_verbose)
			sdmmc_print_cis(sf);
	}
	return 0;
}

/*
 * Indicate whether the function is ready to operate.
 */
int
sdmmc_io_function_ready(struct sdmmc_function *sf)
{
	struct sdmmc_softc *sc = sf->sc;
	struct sdmmc_function *sf0 = sc->sc_fn0;
	u_int8_t rv;

	if (sf->number == 0)
		return 1;	/* FN0 is always ready */

	rv = sdmmc_io_read_1(sf0, SD_IO_CCCR_FN_READY);
	return (rv & (1 << sf->number)) != 0;
}

/*
 * Enable the I/O function.  Return zero if the function was
 * enabled successfully.
 */
int
sdmmc_io_function_enable(struct sdmmc_function *sf)
{
	struct sdmmc_softc *sc = sf->sc;
	struct sdmmc_function *sf0 = sc->sc_fn0;
	u_int8_t rv;
	int retry = 5;

	if (sf->number == 0)
		return 0;	/* FN0 is always enabled */

	SDMMC_LOCK(sc);
	rv = sdmmc_io_read_1(sf0, SD_IO_CCCR_FN_ENABLE);
	rv |= (1<<sf->number);
	sdmmc_io_write_1(sf0, SD_IO_CCCR_FN_ENABLE, rv);
	SDMMC_UNLOCK(sc);

	while (!sdmmc_io_function_ready(sf) && retry-- > 0)
		tsleep(&lbolt, PPAUSE, "pause", 0);
	return (retry >= 0) ? 0 : ETIMEDOUT;
}

/*
 * Disable the I/O function.  Return zero if the function was
 * disabled successfully.
 */
void
sdmmc_io_function_disable(struct sdmmc_function *sf)
{
	struct sdmmc_softc *sc = sf->sc;
	struct sdmmc_function *sf0 = sc->sc_fn0;
	u_int8_t rv;

	if (sf->number == 0)
		return;		/* FN0 is always enabled */

	SDMMC_LOCK(sc);
	rv = sdmmc_io_read_1(sf0, SD_IO_CCCR_FN_ENABLE);
	rv &= ~(1<<sf->number);
	sdmmc_io_write_1(sf0, SD_IO_CCCR_FN_ENABLE, rv);
	SDMMC_UNLOCK(sc);
}

void
sdmmc_io_attach(struct sdmmc_softc *sc)
{
	struct sdmmc_function *sf;
	struct sdmmc_attach_args saa;

	SIMPLEQ_FOREACH(sf, &sc->sf_head, sf_list) {
		if (sf->number < 1)
			continue;

		bzero(&saa, sizeof saa);
		saa.sf = sf;

		sf->child = config_found_sm(&sc->sc_dev, &saa, sdmmc_print,
		    sdmmc_submatch);
	}
}

int
sdmmc_submatch(struct device *parent, void *match, void *aux)
{
	struct cfdata *cf = match;

	/* Skip the scsibus, it is configured directly. */
	if (strcmp(cf->cf_driver->cd_name, "scsibus") == 0)
		return 0;

	return cf->cf_attach->ca_match(parent, cf, aux);
}

int
sdmmc_print(void *aux, const char *pnp)
{
	struct sdmmc_attach_args *sa = aux;
	struct sdmmc_function *sf = sa->sf;
	struct sdmmc_cis *cis = &sf->sc->sc_fn0->cis;
	int i;

	if (pnp) {
		if (sf->number == 0)
			return QUIET;

		for (i = 0; i < 4 && cis->cis1_info[i]; i++)
			printf("%s%s", i ? ", " : "\"", cis->cis1_info[i]);
		if (i != 0)
			printf("\"");

		if (cis->manufacturer != SDMMC_VENDOR_INVALID &&
		    cis->product != SDMMC_PRODUCT_INVALID) {
			printf("%s(", i ? " " : "");
			if (cis->manufacturer != SDMMC_VENDOR_INVALID)
				printf("manufacturer 0x%x%s",
				    cis->manufacturer,
				    cis->product == SDMMC_PRODUCT_INVALID ?
				    "" : ", ");
			if (cis->product != SDMMC_PRODUCT_INVALID)
				printf("product 0x%x", cis->product);
			printf(")");
		}
		printf("%sat %s", i ? " " : "", pnp);
	}
	printf(" function %d", sf->number);

	if (!pnp) {
		for (i = 0; i < 3 && cis->cis1_info[i]; i++)
			printf("%s%s", i ? ", " : " \"", cis->cis1_info[i]);
		if (i != 0)
			printf("\"");
	}
	return UNCONF;
}

void
sdmmc_io_detach(struct sdmmc_softc *sc)
{
	struct sdmmc_function *sf;

	SIMPLEQ_FOREACH(sf, &sc->sf_head, sf_list) {
		if (sf->child != NULL) {
			config_detach(sf->child, DETACH_FORCE);
			sf->child = NULL;
		}
	}

	KASSERT(TAILQ_EMPTY(&sc->sc_intrq));
}

int
sdmmc_io_rw_direct(struct sdmmc_softc *sc, struct sdmmc_function *sf,
    int reg, u_char *datap, int arg)
{
	struct sdmmc_command cmd;
	int error;

	SDMMC_LOCK(sc);

	/* Make sure the card is selected. */
	if ((error = sdmmc_select_card(sc, sf)) != 0) {
		SDMMC_UNLOCK(sc);
		return error;
	}

	arg |= ((sf == NULL ? 0 : sf->number) & SD_ARG_CMD52_FUNC_MASK) <<
	    SD_ARG_CMD52_FUNC_SHIFT;
	arg |= (reg & SD_ARG_CMD52_REG_MASK) <<
	    SD_ARG_CMD52_REG_SHIFT;
	arg |= (*datap & SD_ARG_CMD52_DATA_MASK) <<
	    SD_ARG_CMD52_DATA_SHIFT;

	bzero(&cmd, sizeof cmd);
	cmd.c_opcode = SD_IO_RW_DIRECT;
	cmd.c_arg = arg;
	cmd.c_flags = SCF_CMD_AC | SCF_RSP_R5;

	error = sdmmc_mmc_command(sc, &cmd);
	*datap = SD_R5_DATA(cmd.c_resp);

	SDMMC_UNLOCK(sc);
	return error;
}

/*
 * Useful values of `arg' to pass in are either SD_ARG_CMD53_READ or
 * SD_ARG_CMD53_WRITE.  SD_ARG_CMD53_INCREMENT may be ORed into `arg'
 * to access successive register locations instead of accessing the
 * same register many times.
 */
int
sdmmc_io_rw_extended(struct sdmmc_softc *sc, struct sdmmc_function *sf,
    int reg, u_char *datap, int datalen, int arg)
{
	struct sdmmc_command cmd;
	int error;

	SDMMC_LOCK(sc);

#if 0
	/* Make sure the card is selected. */
	if ((error = sdmmc_select_card(sc, sf)) != 0) {
		SDMMC_UNLOCK(sc);
		return error;
	}
#endif

	arg |= ((sf == NULL ? 0 : sf->number) & SD_ARG_CMD53_FUNC_MASK) <<
	    SD_ARG_CMD53_FUNC_SHIFT;
	arg |= (reg & SD_ARG_CMD53_REG_MASK) <<
	    SD_ARG_CMD53_REG_SHIFT;
	arg |= (datalen & SD_ARG_CMD53_LENGTH_MASK) <<
	    SD_ARG_CMD53_LENGTH_SHIFT;

	bzero(&cmd, sizeof cmd);
	cmd.c_opcode = SD_IO_RW_EXTENDED;
	cmd.c_arg = arg;
	cmd.c_flags = SCF_CMD_AC | SCF_RSP_R5;
	cmd.c_data = datap;
	cmd.c_datalen = datalen;
	cmd.c_blklen = MIN(datalen, sdmmc_chip_host_maxblklen(sc->sct, sc->sch));

	if (!ISSET(arg, SD_ARG_CMD53_WRITE))
		cmd.c_flags |= SCF_CMD_READ;

	error = sdmmc_mmc_command(sc, &cmd);
	SDMMC_UNLOCK(sc);
	return error;
}

u_int8_t
sdmmc_io_read_1(struct sdmmc_function *sf, int reg)
{
	u_int8_t data = 0;
	
	(void)sdmmc_io_rw_direct(sf->sc, sf, reg, (u_char *)&data,
	    SD_ARG_CMD52_READ);
	return data;
}

void
sdmmc_io_write_1(struct sdmmc_function *sf, int reg, u_int8_t data)
{
	(void)sdmmc_io_rw_direct(sf->sc, sf, reg, (u_char *)&data,
	    SD_ARG_CMD52_WRITE);
}

u_int16_t
sdmmc_io_read_2(struct sdmmc_function *sf, int reg)
{
	u_int16_t data = 0;
	
	(void)sdmmc_io_rw_extended(sf->sc, sf, reg, (u_char *)&data, 2,
	    SD_ARG_CMD53_READ | SD_ARG_CMD53_INCREMENT);
	return data;
}

void
sdmmc_io_write_2(struct sdmmc_function *sf, int reg, u_int16_t data)
{
	(void)sdmmc_io_rw_extended(sf->sc, sf, reg, (u_char *)&data, 2,
	    SD_ARG_CMD53_WRITE | SD_ARG_CMD53_INCREMENT);
}

u_int32_t
sdmmc_io_read_4(struct sdmmc_function *sf, int reg)
{
	u_int32_t data = 0;
	
	(void)sdmmc_io_rw_extended(sf->sc, sf, reg, (u_char *)&data, 4,
	    SD_ARG_CMD53_READ | SD_ARG_CMD53_INCREMENT);
	return data;
}

void
sdmmc_io_write_4(struct sdmmc_function *sf, int reg, u_int32_t data)
{
	(void)sdmmc_io_rw_extended(sf->sc, sf, reg, (u_char *)&data, 4,
	    SD_ARG_CMD53_WRITE | SD_ARG_CMD53_INCREMENT);
}

int
sdmmc_io_read_multi_1(struct sdmmc_function *sf, int reg, u_char *data,
    int datalen)
{
	return sdmmc_io_rw_extended(sf->sc, sf, reg, data, datalen,
	    SD_ARG_CMD53_READ);
}

int
sdmmc_io_write_multi_1(struct sdmmc_function *sf, int reg, u_char *data,
    int datalen)
{
	return sdmmc_io_rw_extended(sf->sc, sf, reg, data, datalen,
	    SD_ARG_CMD53_WRITE);
}

int
sdmmc_io_xchg(struct sdmmc_softc *sc, struct sdmmc_function *sf,
    int reg, u_char *datap)
{
	return sdmmc_io_rw_direct(sc, sf, reg, datap,
	    SD_ARG_CMD52_WRITE|SD_ARG_CMD52_EXCHANGE);
}

/*
 * Reset the I/O functions of the card.
 */
void
sdmmc_io_reset(struct sdmmc_softc *sc)
{
#if 0 /* XXX command fails */
	(void)sdmmc_io_write(sc, NULL, SD_IO_REG_CCCR_CTL, CCCR_CTL_RES);
	sdmmc_delay(100000);
#endif
}

/*
 * Get or set the card's I/O OCR value (SDIO).
 */
int
sdmmc_io_send_op_cond(struct sdmmc_softc *sc, u_int32_t ocr, u_int32_t *ocrp)
{
	struct sdmmc_command cmd;
	int error;
	int i;

	SDMMC_LOCK(sc);

	/*
	 * If we change the OCR value, retry the command until the OCR
	 * we receive in response has the "CARD BUSY" bit set, meaning
	 * that all cards are ready for identification.
	 */
	for (i = 0; i < 100; i++) {
		bzero(&cmd, sizeof cmd);
		cmd.c_opcode = SD_IO_SEND_OP_COND;
		cmd.c_arg = ocr;
		cmd.c_flags = SCF_CMD_BCR | SCF_RSP_R4;

		error = sdmmc_mmc_command(sc, &cmd);
		if (error != 0)
			break;
		if (ISSET(MMC_R4(cmd.c_resp), SD_IO_OCR_MEM_READY) ||
		    ocr == 0)
			break;
		error = ETIMEDOUT;
		sdmmc_delay(10000);
	}
	if (error == 0 && ocrp != NULL)
		*ocrp = MMC_R4(cmd.c_resp);

	SDMMC_UNLOCK(sc);
	return error;
}

/*
 * Card interrupt handling
 */

void
sdmmc_intr_enable(struct sdmmc_function *sf)
{
	struct sdmmc_softc *sc = sf->sc;
	struct sdmmc_function *sf0 = sc->sc_fn0;
	u_int8_t imask;

	SDMMC_LOCK(sc);
	imask = sdmmc_io_read_1(sf0, SD_IO_CCCR_INT_ENABLE);
	imask |= 1 << sf->number;
	sdmmc_io_write_1(sf0, SD_IO_CCCR_INT_ENABLE, imask);
	SDMMC_UNLOCK(sc);
}

void
sdmmc_intr_disable(struct sdmmc_function *sf)
{
	struct sdmmc_softc *sc = sf->sc;
	struct sdmmc_function *sf0 = sc->sc_fn0;
	u_int8_t imask;

	SDMMC_LOCK(sc);
	imask = sdmmc_io_read_1(sf0, SD_IO_CCCR_INT_ENABLE);
	imask &= ~(1 << sf->number);
	sdmmc_io_write_1(sf0, SD_IO_CCCR_INT_ENABLE, imask);
	SDMMC_UNLOCK(sc);
}

/*
 * Establish a handler for the SDIO card interrupt.  Because the
 * interrupt may be shared with different SDIO functions, multiple
 * handlers can be established.
 */
void *
sdmmc_intr_establish(struct device *sdmmc, int (*fun)(void *),
    void *arg, const char *name)
{
	struct sdmmc_softc *sc = (struct sdmmc_softc *)sdmmc;
	struct sdmmc_intr_handler *ih;
	int s;

	if (sc->sct->card_intr_mask == NULL)
		return NULL;

	ih = malloc(sizeof *ih, M_DEVBUF, M_WAITOK | M_CANFAIL);
	if (ih == NULL)
		return NULL;

	bzero(ih, sizeof *ih);
	ih->ih_name = malloc(strlen(name), M_DEVBUF, M_WAITOK | M_CANFAIL);
	if (ih->ih_name == NULL) {
		free(ih, M_DEVBUF);
		return NULL;
	}
	strlcpy(ih->ih_name, name, strlen(name));
	ih->ih_softc = sc;
	ih->ih_fun = fun;
	ih->ih_arg = arg;

	s = splhigh();
	if (TAILQ_EMPTY(&sc->sc_intrq)) {
		sdmmc_intr_enable(sc->sc_fn0);
		sdmmc_chip_card_intr_mask(sc->sct, sc->sch, 1);
	}
	TAILQ_INSERT_TAIL(&sc->sc_intrq, ih, entry);
	splx(s);
	return ih;
}

/*
 * Disestablish the given handler.
 */
void
sdmmc_intr_disestablish(void *cookie)
{
	struct sdmmc_intr_handler *ih = cookie;
	struct sdmmc_softc *sc = ih->ih_softc;
	int s;

	if (sc->sct->card_intr_mask == NULL)
		return;

	s = splhigh();
	TAILQ_REMOVE(&sc->sc_intrq, ih, entry);
	if (TAILQ_EMPTY(&sc->sc_intrq)) {
		sdmmc_chip_card_intr_mask(sc->sct, sc->sch, 0);
		sdmmc_intr_disable(sc->sc_fn0);
	}
	splx(s);

	free(ih->ih_name, M_DEVBUF);
	free(ih, M_DEVBUF);
}

/*
 * Call established SDIO card interrupt handlers.  The host controller
 * must call this function from its own interrupt handler to handle an
 * SDIO interrupt from the card.
 */
void
sdmmc_card_intr(struct device *sdmmc)
{
	struct sdmmc_softc *sc = (struct sdmmc_softc *)sdmmc;

	if (sc->sct->card_intr_mask == NULL)
		return;

	if (!sdmmc_task_pending(&sc->sc_intr_task))
		sdmmc_add_task(sc, &sc->sc_intr_task);
}

void
sdmmc_intr_task(void *arg)
{
	struct sdmmc_softc *sc = arg;
	struct sdmmc_intr_handler *ih;
	int s;

	s = splhigh();
	TAILQ_FOREACH(ih, &sc->sc_intrq, entry) {
		splx(s);

		/* XXX examine return value and do evcount stuff*/
		(void)ih->ih_fun(ih->ih_arg);

		s = splhigh();
	}
	sdmmc_chip_card_intr_ack(sc->sct, sc->sch);
	splx(s);
}
