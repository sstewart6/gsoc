// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2008, Freescale Semiconductor, Inc
 * Andy Fleming
 *
 * Based vaguely on the Linux code
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <sys/errno.h>
#include "mmc.h"
#include "bcm2835_sdhci.h"
#include <minix/mmio.h>
#include <minix/sysutil.h>
#include <minix/log.h>

#define DEFAULT_CMD6_TIMEOUT_MS  500

typedef unsigned long lbaint_t;
#define udelay(X) micro_delay(X)
#define mdelay(X) micro_delay(X)
int micro_delay(u32_t);

static int mmc_set_signal_voltage(struct mmc *mmc, uint signal_voltage);
static int mmc_power_cycle(struct mmc *mmc);

/* used for logging */
static struct log log = {
	.name = "mmc.c",
	.log_level = LEVEL_INFO,
	.log_func = default_log
};

static int mmc_wait_dat0(struct mmc *mmc, int state, int timeout_us)
{
    return -ENOSYS;
}

__weak int board_mmc_getwp(struct mmc *mmc)
{
    return -1;
}

int mmc_getwp(struct mmc *mmc)
{
    int wp;
    wp = board_mmc_getwp(mmc);

    if (wp < 0) {
        if (mmc->cfg->ops->getwp)
            wp = mmc->cfg->ops->getwp(mmc);
        else
            wp = 0;
    }

    return wp;
}

__weak int board_mmc_getcd(struct mmc *mmc)
{
    return -1;
}

#ifdef CONFIG_MMC_TRACE
void mmmc_trace_before_send(struct mmc *mmc, struct mmc_cmd *cmd)
{
	printf("CMD_SEND:%d\n", cmd->cmdidx);
	printf("\t\tARG\t\t\t 0x%08x\n", cmd->cmdarg);
}

void mmmc_trace_after_send(struct mmc *mmc, struct mmc_cmd *cmd, int ret)
{
	int i;
	u8 *ptr;

	if (ret) {
		printf("\t\tRET\t\t\t %d\n", ret);
	} else {
		switch (cmd->resp_type) {
		case MMC_RSP_NONE:
			printf("\t\tMMC_RSP_NONE\n");
			break;
		case MMC_RSP_R1:
			printf("\t\tMMC_RSP_R1,5,6,7 \t 0x%08x \n",
				cmd->response[0]);
			break;
		case MMC_RSP_R1b:
			printf("\t\tMMC_RSP_R1b\t\t 0x%08x \n",
				cmd->response[0]);
			break;
		case MMC_RSP_R2:
			printf("\t\tMMC_RSP_R2\t\t 0x%08x \n",
				cmd->response[0]);
			printf("\t\t          \t\t 0x%08x \n",
				cmd->response[1]);
			printf("\t\t          \t\t 0x%08x \n",
				cmd->response[2]);
			printf("\t\t          \t\t 0x%08x \n",
				cmd->response[3]);
			printf("\n");
			printf("\t\t\t\t\tDUMPING DATA\n");
			for (i = 0; i < 4; i++) {
				int j;
				printf("\t\t\t\t\t%03d - ", i*4);
				ptr = (u8 *)&cmd->response[i];
				ptr += 3;
				for (j = 0; j < 4; j++)
					printf("%02x ", *ptr--);
				printf("\n");
			}
			break;
		case MMC_RSP_R3:
			printf("\t\tMMC_RSP_R3,4\t\t 0x%08x \n",
				cmd->response[0]);
			break;
		default:
			printf("\t\tERROR MMC rsp not supported\n");
			break;
		}
	}
}

void mmc_trace_state(struct mmc *mmc, struct mmc_cmd *cmd)
{
	int status;

	status = (cmd->response[0] & MMC_STATUS_CURR_STATE) >> 9;
	printf("CURR STATE:%d\n", status);
}
#endif

const char *mmc_mode_name(enum bus_mode mode)
{
	static const char *const names[] = {
	      [MMC_LEGACY]	= "MMC legacy",
	      [SD_LEGACY]	= "SD Legacy",
	      [MMC_HS]		= "MMC High Speed (26MHz)",
	      [SD_HS]		= "SD High Speed (50MHz)",
	      [UHS_SDR12]	= "UHS SDR12 (25MHz)",
	      [UHS_SDR25]	= "UHS SDR25 (50MHz)",
	      [UHS_SDR50]	= "UHS SDR50 (100MHz)",
	      [UHS_SDR104]	= "UHS SDR104 (208MHz)",
	      [UHS_DDR50]	= "UHS DDR50 (50MHz)",
	      [MMC_HS_52]	= "MMC High Speed (52MHz)",
	      [MMC_DDR_52]	= "MMC DDR52 (52MHz)",
	      [MMC_HS_200]	= "HS200 (200MHz)",
	      [MMC_HS_400]	= "HS400 (200MHz)",
	      [MMC_HS_400_ES]	= "HS400ES (200MHz)",
	};

	if (mode >= MMC_MODES_END)
		return "Unknown mode";
	else
		return names[mode];
}

static uint mmc_mode2freq(struct mmc *mmc, enum bus_mode mode)
{
	static const int freqs[] = {
	      [MMC_LEGACY]	= 25000000,
	      [SD_LEGACY]	= 25000000,
	      [MMC_HS]		= 26000000,
	      [SD_HS]		= 50000000,
	      [MMC_HS_52]	= 52000000,
	      [MMC_DDR_52]	= 52000000,
	      [UHS_SDR12]	= 25000000,
	      [UHS_SDR25]	= 50000000,
	      [UHS_SDR50]	= 100000000,
	      [UHS_DDR50]	= 50000000,
	      [UHS_SDR104]	= 208000000,
	      [MMC_HS_200]	= 200000000,
	      [MMC_HS_400]	= 200000000,
	      [MMC_HS_400_ES]	= 200000000,
	};

	if (mode == MMC_LEGACY)
		return mmc->legacy_speed;
	else if (mode >= MMC_MODES_END)
		return 0;
	else
		return freqs[mode];
}

static int mmc_select_mode(struct mmc *mmc, enum bus_mode mode)
{
	mmc->selected_mode = mode;
	mmc->tran_speed = mmc_mode2freq(mmc, mode);
	mmc->ddr_mode = mmc_is_mode_ddr(mode);
	log_warn(&log, "selecting mode %s (freq : %d MHz)\n", mmc_mode_name(mode),
		 mmc->tran_speed / 1000000);
	return 0;
}

int mmc_send_cmd(struct mmc *mmc, struct mmc_cmd *cmd, struct mmc_data *data)
{
	int ret;

#ifdef CONFIG_MMC_TRACE
	mmmc_trace_before_send(mmc, cmd);
#endif
	ret = mmc->cfg->ops->send_cmd(mmc, cmd, data);
#ifdef CONFIG_MMC_TRACE
	mmmc_trace_after_send(mmc, cmd, ret);
#endif

	return ret;
}

int mmc_send_status(struct mmc *mmc, unsigned int *status)
{
	struct mmc_cmd cmd;
	int err, retries = 5;

	cmd.cmdidx = MMC_CMD_SEND_STATUS;
	cmd.resp_type = MMC_RSP_R1;
	if (!mmc_host_is_spi(mmc))
		cmd.cmdarg = mmc->rca << 16;

	while (retries--) {
		err = mmc_send_cmd(mmc, &cmd, NULL);
		if (!err) {
#ifdef CONFIG_MMC_TRACE
			mmc_trace_state(mmc, &cmd);
#endif
			*status = cmd.response[0];
			return 0;
		}
	}
#ifdef CONFIG_MMC_TRACE
	mmc_trace_state(mmc, &cmd);
#endif
	return -ETIMEDOUT;
}

int mmc_poll_for_busy(struct mmc *mmc, int timeout_ms)
{
	unsigned int status;
	int err;

	err = mmc_wait_dat0(mmc, 1, timeout_ms * 1000);
	if (err != -ENOSYS)
		return err;

	while (1) {
		err = mmc_send_status(mmc, &status);
		if (err)
			return err;

		if ((status & MMC_STATUS_RDY_FOR_DATA) &&
		    (status & MMC_STATUS_CURR_STATE) !=
		     MMC_STATE_PRG)
			break;

		if (status & MMC_STATUS_MASK) {
			log_warn(&log, "Status Error: 0x%08x\n", status);
			return -ENOTSUP;
		}

		if (timeout_ms-- <= 0)
			break;

		udelay(1000);
	}

	if (timeout_ms <= 0) {
		log_warn(&log, "Timeout waiting card ready\n");
		return -ETIMEDOUT;
	}

	return 0;
}

int mmc_set_blocklen(struct mmc *mmc, int len)
{
	struct mmc_cmd cmd;
	int err;

	if (mmc->ddr_mode)
		return 0;

	cmd.cmdidx = MMC_CMD_SET_BLOCKLEN;
	cmd.resp_type = MMC_RSP_R1;
	cmd.cmdarg = len;

	err = mmc_send_cmd(mmc, &cmd, NULL);

	if (err && (mmc->quirks & MMC_QUIRK_RETRY_SET_BLOCKLEN)) {
		int retries = 4;
		/*
		 * It has been seen that SET_BLOCKLEN may fail on the first
		 * attempt, let's try a few more time
		 */
		do {
			err = mmc_send_cmd(mmc, &cmd, NULL);
			if (!err)
				break;
		} while (retries--);
	}

	return err;
}

static int mmc_read_blocks(struct mmc *mmc, void *dst, lbaint_t start,
			   lbaint_t blkcnt)
{
	struct mmc_cmd cmd;
	struct mmc_data data;

	if (blkcnt > 1)
		cmd.cmdidx = MMC_CMD_READ_MULTIPLE_BLOCK;
	else
		cmd.cmdidx = MMC_CMD_READ_SINGLE_BLOCK;

	if (mmc->high_capacity)
		cmd.cmdarg = start;
	else
		cmd.cmdarg = start * mmc->read_bl_len;

	cmd.resp_type = MMC_RSP_R1;

	data.dest = dst;
	data.blocks = blkcnt;
	data.blocksize = mmc->read_bl_len;
	data.flags = MMC_DATA_READ;

	if (mmc_send_cmd(mmc, &cmd, &data))
		return 0;

	if (blkcnt > 1) {
		cmd.cmdidx = MMC_CMD_STOP_TRANSMISSION;
		cmd.cmdarg = 0;
		cmd.resp_type = MMC_RSP_R1b;
		if (mmc_send_cmd(mmc, &cmd, NULL)) {
			log_warn(&log, "mmc fail to send stop cmd\n");
			return 0;
		}
	}

	return blkcnt;
}

ulong mmc_bread(struct mmc *mmc, lbaint_t start, lbaint_t blkcnt,
		void *dst)
{
	int err;
	lbaint_t cur, blocks_todo = blkcnt;
	u32_t blk_capacity = mmc->capacity / mmc->read_bl_len;

	if (blkcnt == 0)
		return 0;

	if (!mmc)
		return 0;

	if ((start + blkcnt) > blk_capacity) {
		log_warn(&log, "MMC: block number 0x%08x exceeds max(0x%08x)\n",
		       start + blkcnt, blk_capacity);
		return 0;
	}

	if (mmc_set_blocklen(mmc, mmc->read_bl_len)) {
		log_warn(&log, "%s: Failed to set blocklen\n", __func__);
		return 0;
	}

	do {
		cur = (blocks_todo > mmc->cfg->b_max) ?
			mmc->cfg->b_max : blocks_todo;
		if (mmc_read_blocks(mmc, dst, start, cur) != cur) {
			log_warn(&log, "%s: Failed to read blocks\n", __func__);
			return 0;
		}
		blocks_todo -= cur;
		start += cur;
		dst += cur * mmc->read_bl_len;
	} while (blocks_todo > 0);

	return blkcnt;
}

static int mmc_go_idle(struct mmc *mmc)
{
	struct mmc_cmd cmd;
	int err;

	udelay(1000);

	cmd.cmdidx = MMC_CMD_GO_IDLE_STATE;
	cmd.cmdarg = 0;
	cmd.resp_type = MMC_RSP_NONE;

	err = mmc_send_cmd(mmc, &cmd, NULL);

	if (err)
		return err;

	udelay(2000);

	return 0;
}

static int sd_send_op_cond(struct mmc *mmc, bool uhs_en)
{
	/* int timeout = 1000; */
	int timeout = 10;
	int err;
	struct mmc_cmd cmd;

	while (1) {
		cmd.cmdidx = MMC_CMD_APP_CMD;
		cmd.resp_type = MMC_RSP_R1;
		cmd.cmdarg = 0;

		err = mmc_send_cmd(mmc, &cmd, NULL);

		if (err)
			return err;

		cmd.cmdidx = SD_CMD_APP_SEND_OP_COND;
		cmd.resp_type = MMC_RSP_R3;

		/*
		 * Most cards do not answer if some reserved bits
		 * in the ocr are set. However, Some controller
		 * can set bit 7 (reserved for low voltages), but
		 * how to manage low voltages SD card is not yet
		 * specified.
		 */
		cmd.cmdarg = mmc_host_is_spi(mmc) ? 0 :
			(mmc->cfg->voltages & 0xff8000);

		if (mmc->version == SD_VERSION_2)
			cmd.cmdarg |= OCR_HCS;

		if (uhs_en)
			cmd.cmdarg |= OCR_S18R;

		err = mmc_send_cmd(mmc, &cmd, NULL);

		if (err)
			return err;

		if (cmd.response[0] & OCR_BUSY)
			break;

		if (timeout-- <= 0)
			return -EOPNOTSUPP;

		udelay(1000);
	}

	if (mmc->version != SD_VERSION_2)
		mmc->version = SD_VERSION_1_0;

	if (mmc_host_is_spi(mmc)) { /* read OCR for spi */
		cmd.cmdidx = MMC_CMD_SPI_READ_OCR;
		cmd.resp_type = MMC_RSP_R3;
		cmd.cmdarg = 0;

		err = mmc_send_cmd(mmc, &cmd, NULL);

		if (err)
			return err;
	}

	mmc->ocr = cmd.response[0];

	mmc->high_capacity = ((mmc->ocr & OCR_HCS) == OCR_HCS);
	mmc->rca = 0;

	return 0;
}

static int mmc_send_op_cond_iter(struct mmc *mmc, int use_arg)
{
	struct mmc_cmd cmd;
	int err;

	cmd.cmdidx = MMC_CMD_SEND_OP_COND;
	cmd.resp_type = MMC_RSP_R3;
	cmd.cmdarg = 0;
	if (use_arg && !mmc_host_is_spi(mmc))
		cmd.cmdarg = OCR_HCS |
			(mmc->cfg->voltages &
			(mmc->ocr & OCR_VOLTAGE_MASK)) |
			(mmc->ocr & OCR_ACCESS_MODE);

	err = mmc_send_cmd(mmc, &cmd, NULL);
	if (err)
		return err;
	mmc->ocr = cmd.response[0];
	return 0;
}

static int mmc_send_op_cond(struct mmc *mmc)
{
	int err, i;

	/* Some cards seem to need this */
	mmc_go_idle(mmc);

 	/* Asking to the card its capabilities */
	for (i = 0; i < 2; i++) {
		err = mmc_send_op_cond_iter(mmc, i != 0);
		if (err)
			return err;

		/* exit if not busy (flag seems to be inverted) */
		if (mmc->ocr & OCR_BUSY)
			break;
	}
	mmc->op_cond_pending = 1;
	return 0;
}

static int mmc_complete_op_cond(struct mmc *mmc)
{
	struct mmc_cmd cmd;
	int timeout = 1000;
	u32_t start, end;
	int err;

	mmc->op_cond_pending = 0;
	if (!(mmc->ocr & OCR_BUSY)) {
		/* Some cards seem to need this */
		mmc_go_idle(mmc);

		read_frclock(&start);
		while (1) {
			err = mmc_send_op_cond_iter(mmc, 1);
			if (err)
				return err;
			if (mmc->ocr & OCR_BUSY)
				break;
			if (delta_frclock(start, end) > timeout)
				return -EOPNOTSUPP;
			udelay(100);
			read_frclock(&end);
		}
	}

	if (mmc_host_is_spi(mmc)) { /* read OCR for spi */
		cmd.cmdidx = MMC_CMD_SPI_READ_OCR;
		cmd.resp_type = MMC_RSP_R3;
		cmd.cmdarg = 0;

		err = mmc_send_cmd(mmc, &cmd, NULL);

		if (err)
			return err;

		mmc->ocr = cmd.response[0];
	}

	mmc->version = MMC_VERSION_UNKNOWN;

	mmc->high_capacity = ((mmc->ocr & OCR_HCS) == OCR_HCS);
	mmc->rca = 1;

	return 0;
}


static int mmc_send_ext_csd(struct mmc *mmc, u8 *ext_csd)
{
	struct mmc_cmd cmd;
	struct mmc_data data;
	int err;

	/* Get the Card Status Register */
	cmd.cmdidx = MMC_CMD_SEND_EXT_CSD;
	cmd.resp_type = MMC_RSP_R1;
	cmd.cmdarg = 0;

	data.dest = (char *)ext_csd;
	data.blocks = 1;
	data.blocksize = MMC_MAX_BLOCK_LEN;
	data.flags = MMC_DATA_READ;

	err = mmc_send_cmd(mmc, &cmd, &data);

	return err;
}

static int __mmc_switch(struct mmc *mmc, u8 set, u8 index, u8 value,
			bool send_status)
{
	unsigned int status;
	u32_t start, end;
	struct mmc_cmd cmd;
	int timeout_ms = DEFAULT_CMD6_TIMEOUT_MS;
	bool is_part_switch = (set == EXT_CSD_CMD_SET_NORMAL) &&
			      (index == EXT_CSD_PART_CONF);
	int retries = 3;
	int ret;

	if (mmc->gen_cmd6_time)
		timeout_ms = mmc->gen_cmd6_time * 10;

	if (is_part_switch  && mmc->part_switch_time)
		timeout_ms = mmc->part_switch_time * 10;

	cmd.cmdidx = MMC_CMD_SWITCH;
	cmd.resp_type = MMC_RSP_R1b;
	cmd.cmdarg = (MMC_SWITCH_MODE_WRITE_BYTE << 24) |
				 (index << 16) |
				 (value << 8);

	do {
		ret = mmc_send_cmd(mmc, &cmd, NULL);
	} while (ret && retries-- > 0);

	if (ret)
		return ret;

	read_frclock(&start);

	/* poll dat0 for rdy/buys status */
	ret = mmc_wait_dat0(mmc, 1, timeout_ms * 1000);
	if (ret && ret != -ENOSYS)
		return ret;

	/*
	 * In cases when not allowed to poll by using CMD13 or because we aren't
	 * capable of polling by using mmc_wait_dat0, then rely on waiting the
	 * stated timeout to be sufficient.
	 */
	if (ret == -ENOSYS && !send_status)
		mdelay(timeout_ms);

	/* Finally wait until the card is ready or indicates a failure
	 * to switch. It doesn't hurt to use CMD13 here even if send_status
	 * is false, because by now (after 'timeout_ms' ms) the bus should be
	 * reliable.
	 */
	do {
		ret = mmc_send_status(mmc, &status);

		if (!ret && (status & MMC_STATUS_SWITCH_ERROR)) {
			log_warn(&log, "switch failed %d/%d/0x%x !\n", set, index,
				 value);
			return -EIO;
		}
		if (!ret && (status & MMC_STATUS_RDY_FOR_DATA))
			return 0;
		udelay(100);
		read_frclock(&end);
	} while (delta_frclock(start, end) < timeout_ms * MILLA_TO_MICRO);

	return -ETIMEDOUT;
}

int mmc_switch(struct mmc *mmc, u8 set, u8 index, u8 value)
{
	return __mmc_switch(mmc, set, index, value, true);
}

static int mmc_set_card_speed(struct mmc *mmc, enum bus_mode mode,
			      bool hsdowngrade)
{
	int err;
	int speed_bits;
	u8 test_csd[MMC_MAX_BLOCK_LEN] __attribute__ ((aligned (64)));

	switch (mode) {
	case MMC_HS:
	case MMC_HS_52:
	case MMC_DDR_52:
		speed_bits = EXT_CSD_TIMING_HS;
		break;
	case MMC_LEGACY:
		speed_bits = EXT_CSD_TIMING_LEGACY;
		break;
	default:
		return -EINVAL;
	}

	err = __mmc_switch(mmc, EXT_CSD_CMD_SET_NORMAL, EXT_CSD_HS_TIMING,
			   speed_bits, !hsdowngrade);
	if (err)
		return err;

	if ((mode == MMC_HS) || (mode == MMC_HS_52)) {
		/* Now check to see that it worked */
		err = mmc_send_ext_csd(mmc, test_csd);
		if (err)
			return err;

		/* No high-speed support */
		if (!test_csd[EXT_CSD_HS_TIMING])
			return -EOPNOTSUPP;
	}

	return 0;
}

static int mmc_get_capabilities(struct mmc *mmc)
{
	u8 *ext_csd = mmc->ext_csd;
	char cardtype;

	mmc->card_caps = MMC_MODE_1BIT | MMC_CAP(MMC_LEGACY);

	if (mmc_host_is_spi(mmc))
		return 0;

	/* Only version 4 supports high-speed */
	if (mmc->version < MMC_VERSION_4)
		return 0;

	if (!ext_csd) {
		log_warn(&log, "No ext_csd found!\n"); /* this should enver happen */
		return -EOPNOTSUPP;
	}

	mmc->card_caps |= MMC_MODE_4BIT | MMC_MODE_8BIT;

	cardtype = ext_csd[EXT_CSD_CARD_TYPE];
	mmc->cardtype = cardtype;

	if (cardtype & EXT_CSD_CARD_TYPE_52) {
		if (cardtype & EXT_CSD_CARD_TYPE_DDR_52)
			mmc->card_caps |= MMC_MODE_DDR_52MHz;
		mmc->card_caps |= MMC_MODE_HS_52MHz;
	}
	if (cardtype & EXT_CSD_CARD_TYPE_26)
		mmc->card_caps |= MMC_MODE_HS;

	return 0;
}

static int mmc_set_capacity(struct mmc *mmc, int part_num)
{
	switch (part_num) {
	case 0:
		mmc->capacity = mmc->capacity_user;
		break;
	case 1:
	case 2:
		mmc->capacity = mmc->capacity_boot;
		break;
	case 3:
		mmc->capacity = mmc->capacity_rpmb;
		break;
	case 4:
	case 5:
	case 6:
	case 7:
		mmc->capacity = mmc->capacity_gp[part_num - 4];
		break;
	default:
		return -1;
	}

	return 0;
}

int mmc_switch_part(struct mmc *mmc, unsigned int part_num)
{
	int ret;
	int retry = 3;

	do {
		ret = mmc_switch(mmc, EXT_CSD_CMD_SET_NORMAL,
				 EXT_CSD_PART_CONF,
				 (mmc->part_config & ~PART_ACCESS_MASK)
				 | (part_num & PART_ACCESS_MASK));
	} while (ret && retry--);

	/*
	 * Set the capacity if the switch succeeded or was intended
	 * to return to representing the raw device.
	 */
	if ((ret == 0) || ((ret == -ENODEV) && (part_num == 0))) {
		ret = mmc_set_capacity(mmc, part_num);
	}

	return ret;
}

int mmc_getcd(struct mmc *mmc)
{
	int cd;

	cd = board_mmc_getcd(mmc);

	if (cd < 0) {
		if (mmc->cfg->ops->getcd)
			cd = mmc->cfg->ops->getcd(mmc);
		else
			cd = 1;
	}

	return cd;
}

static int sd_switch(struct mmc *mmc, int mode, int group, u8 value, u8 *resp)
{
	struct mmc_cmd cmd;
	struct mmc_data data;

	/* Switch the frequency */
	cmd.cmdidx = SD_CMD_SWITCH_FUNC;
	cmd.resp_type = MMC_RSP_R1;
	cmd.cmdarg = (mode << 31) | 0xffffff;
	cmd.cmdarg &= ~(0xf << (group * 4));
	cmd.cmdarg |= value << (group * 4);

	data.dest = (char *)resp;
	data.blocksize = 64;
	data.blocks = 1;
	data.flags = MMC_DATA_READ;

	return mmc_send_cmd(mmc, &cmd, &data);
}

static int sd_get_capabilities(struct mmc *mmc)
{
	int err;
	struct mmc_cmd cmd;
	u32_t scr[2] __attribute__ ((aligned(64)));
	u32_t switch_status[16] __attribute__ ((aligned(64)));
	struct mmc_data data;
	int timeout;

	mmc->card_caps = MMC_MODE_1BIT | MMC_CAP(SD_LEGACY);

	if (mmc_host_is_spi(mmc))
		return 0;

	/* Read the SCR to find out if this card supports higher speeds */
	cmd.cmdidx = MMC_CMD_APP_CMD;
	cmd.resp_type = MMC_RSP_R1;
	cmd.cmdarg = mmc->rca << 16;

	err = mmc_send_cmd(mmc, &cmd, NULL);

	if (err)
		return err;

	cmd.cmdidx = SD_CMD_APP_SEND_SCR;
	cmd.resp_type = MMC_RSP_R1;
	cmd.cmdarg = 0;

	timeout = 3;

retry_scr:
	data.dest = (char *)scr;
	data.blocksize = 8;
	data.blocks = 1;
	data.flags = MMC_DATA_READ;

	err = mmc_send_cmd(mmc, &cmd, &data);

	if (err) {
		if (timeout--)
			goto retry_scr;

		return err;
	}

	mmc->scr[0] = be32_to_le(scr[0]);
	mmc->scr[1] = be32_to_le(scr[1]);

	switch ((mmc->scr[0] >> 24) & 0xf) {
	case 0:
		mmc->version = SD_VERSION_1_0;
		break;
	case 1:
		mmc->version = SD_VERSION_1_10;
		break;
	case 2:
		mmc->version = SD_VERSION_2;
		if ((mmc->scr[0] >> 15) & 0x1)
			mmc->version = SD_VERSION_3;
		break;
	default:
		mmc->version = SD_VERSION_1_0;
		break;
	}

	if (mmc->scr[0] & SD_DATA_4BIT)
		mmc->card_caps |= MMC_MODE_4BIT;

	/* Version 1.0 doesn't support switching */
	if (mmc->version == SD_VERSION_1_0)
		return 0;

	timeout = 4;
	while (timeout--) {
		err = sd_switch(mmc, SD_SWITCH_CHECK, 0, 1,
				(u8 *)switch_status);

		if (err)
			return err;

		/* The high-speed function is busy.  Try again */
		if (!(be32_to_le(switch_status[7]) & SD_HIGHSPEED_BUSY))
			break;
	}

	/* If high-speed isn't supported, we return */
	if (be32_to_le(switch_status[3]) & SD_HIGHSPEED_SUPPORTED)
		mmc->card_caps |= MMC_CAP(SD_HS);

	return 0;
}

static int sd_set_card_speed(struct mmc *mmc, enum bus_mode mode)
{
	int err;
	uint switch_status[16] __attribute__ ((aligned(64)));
	int speed;

	/* SD version 1.00 and 1.01 does not support CMD 6 */
	if (mmc->version == SD_VERSION_1_0)
		return 0;

	switch (mode) {
	case SD_LEGACY:
		speed = UHS_SDR12_BUS_SPEED;
		break;
	case SD_HS:
		speed = HIGH_SPEED_BUS_SPEED;
		break;
	default:
		return -EINVAL;
	}

	err = sd_switch(mmc, SD_SWITCH_SWITCH, 0, speed, (u8 *)switch_status);
	if (err)
		return err;

	if (((be32_to_le(switch_status[4]) >> 24) & 0xF) != speed)
		return -ENOTSUP;

	return 0;
}

static int sd_select_bus_width(struct mmc *mmc, int w)
{
	int err;
	struct mmc_cmd cmd;

	if ((w != 4) && (w != 1))
		return -EINVAL;

	cmd.cmdidx = MMC_CMD_APP_CMD;
	cmd.resp_type = MMC_RSP_R1;
	cmd.cmdarg = mmc->rca << 16;

	err = mmc_send_cmd(mmc, &cmd, NULL);
	if (err)
		return err;

	cmd.cmdidx = SD_CMD_APP_SET_BUS_WIDTH;
	cmd.resp_type = MMC_RSP_R1;
	if (w == 4)
		cmd.cmdarg = 2;
	else if (w == 1)
		cmd.cmdarg = 0;
	err = mmc_send_cmd(mmc, &cmd, NULL);
	if (err)
		return err;

	return 0;
}

static int sd_read_ssr(struct mmc *mmc)
{
	static const unsigned int sd_au_size[] = {
		0,		SZ_16K / 512,		SZ_32K / 512,
		SZ_64K / 512,	SZ_128K / 512,		SZ_256K / 512,
		SZ_512K / 512,	SZ_1M / 512,		SZ_2M / 512,
		SZ_4M / 512,	SZ_8M / 512,		(SZ_8M + SZ_4M) / 512,
		SZ_16M / 512,	(SZ_16M + SZ_8M) / 512,	SZ_32M / 512,
		SZ_64M / 512,
	};
	int err, i;
	struct mmc_cmd cmd;
	uint ssr[16] __attribute__ ((aligned(64)));
	struct mmc_data data;
	int timeout = 3;
	unsigned int au, eo, et, es;

	cmd.cmdidx = MMC_CMD_APP_CMD;
	cmd.resp_type = MMC_RSP_R1;
	cmd.cmdarg = mmc->rca << 16;

	err = mmc_send_cmd(mmc, &cmd, NULL);
	if (err && (mmc->quirks & MMC_QUIRK_RETRY_APP_CMD)) {
		int retries = 4;
		/*
		 * It has been seen that APP_CMD may fail on the first
		 * attempt, let's try a few more times
		 */
		do {
			err = mmc_send_cmd(mmc, &cmd, NULL);
			if (!err)
				break;
		} while (retries--);
	}
	if (err)
		return err;

	cmd.cmdidx = SD_CMD_APP_SD_STATUS;
	cmd.resp_type = MMC_RSP_R1;
	cmd.cmdarg = 0;

retry_ssr:
	data.dest = (char *)ssr;
	data.blocksize = 64;
	data.blocks = 1;
	data.flags = MMC_DATA_READ;

	err = mmc_send_cmd(mmc, &cmd, &data);
	if (err) {
		if (timeout--)
			goto retry_ssr;

		return err;
	}

	for (i = 0; i < 16; i++)
		ssr[i] = be32_to_le(ssr[i]);

	au = (ssr[2] >> 12) & 0xF;
	if ((au <= 9) || (mmc->version == SD_VERSION_3)) {
		mmc->ssr.au = sd_au_size[au];
		es = (ssr[3] >> 24) & 0xFF;
		es |= (ssr[2] & 0xFF) << 8;
		et = (ssr[3] >> 18) & 0x3F;
		if (es && et) {
			eo = (ssr[3] >> 16) & 0x3;
			mmc->ssr.erase_timeout = (et * 1000) / es;
			mmc->ssr.erase_offset = eo * 1000;
		}
	} else {
		log_warn(&log, "Invalid Allocation Unit Size.\n");
	}

	return 0;
}

/* frequency bases */
/* divided by 10 to be nice to platforms without floating point */
static const int fbase[] = {
	10000,
	100000,
	1000000,
	10000000,
};

/* Multiplier values for TRAN_SPEED.  Multiplied by 10 to be nice
 * to platforms without floating point.
 */
static const u8 multipliers[] = {
	0,	/* reserved */
	10,
	12,
	13,
	15,
	20,
	25,
	30,
	35,
	40,
	45,
	50,
	55,
	60,
	70,
	80,
};

static inline int bus_width(uint cap)
{
	if (cap == MMC_MODE_8BIT)
		return 8;
	if (cap == MMC_MODE_4BIT)
		return 4;
	if (cap == MMC_MODE_1BIT)
		return 1;
	log_info(&log, "invalid bus witdh capability 0x%x\n", cap);
	return 0;
}

static int mmc_set_ios(struct mmc *mmc)
{
    int ret = 0;

    if (mmc->cfg->ops->set_ios)
        ret = mmc->cfg->ops->set_ios(mmc);

    return ret;
}

static int mmc_host_power_cycle(struct mmc *mmc)
{
    int ret = 0;

    if (mmc->cfg->ops->host_power_cycle)
        ret = mmc->cfg->ops->host_power_cycle(mmc);

    return ret;
}

int mmc_set_clock(struct mmc *mmc, uint clock, bool disable)
{
	if (!disable) {
		if (clock > mmc->cfg->f_max)
			clock = mmc->cfg->f_max;

		if (clock < mmc->cfg->f_min)
			clock = mmc->cfg->f_min;
	}

	mmc->clock = clock;
	mmc->clk_disable = disable;

	log_info(&log, "clock is %s (%dHz)\n", disable ? "disabled" : "enabled", clock);

	return mmc_set_ios(mmc);
}

static int mmc_set_bus_width(struct mmc *mmc, uint width)
{
	mmc->bus_width = width;

	return mmc_set_ios(mmc);
}

/*
 * helper function to display the capabilities in a human
 * friendly manner. The capabilities include bus width and
 * supported modes.
 */
void mmc_dump_capabilities(const char *text, uint caps)
{
	enum bus_mode mode;

	log_info(&log, "%s: widths [", text);
	if (caps & MMC_MODE_8BIT)
		log_info(&log, "8, ");
	if (caps & MMC_MODE_4BIT)
		log_info(&log, "4, ");
	if (caps & MMC_MODE_1BIT)
		log_info(&log, "1, ");
	log_info(&log, "\b\b] modes [");
	for (mode = MMC_LEGACY; mode < MMC_MODES_END; mode++)
		if (MMC_CAP(mode) & caps)
			log_info(&log, "%s, ", mmc_mode_name(mode));
	log_info(&log, "\b\b]\n");
}

struct mode_width_tuning {
	enum bus_mode mode;
	uint widths;
};

static inline int mmc_set_signal_voltage(struct mmc *mmc, uint signal_voltage)
{
	return 0;
}

static const struct mode_width_tuning sd_modes_by_pref[] = {
	{
		.mode = SD_HS,
		.widths = MMC_MODE_4BIT | MMC_MODE_1BIT,
	},
	{
		.mode = SD_LEGACY,
		.widths = MMC_MODE_4BIT | MMC_MODE_1BIT,
	}
};

#define for_each_sd_mode_by_pref(caps, mwt) \
	for (mwt = sd_modes_by_pref;\
	     mwt < sd_modes_by_pref + ARRAY_SIZE(sd_modes_by_pref);\
	     mwt++) \
		if (caps & MMC_CAP(mwt->mode))

static int sd_select_mode_and_width(struct mmc *mmc, uint card_caps)
{
	int err;
	uint widths[] = {MMC_MODE_4BIT, MMC_MODE_1BIT};
	const struct mode_width_tuning *mwt;
	bool uhs_en = false;
	uint caps;

#ifdef DEBUG
	mmc_dump_capabilities("sd card", card_caps);
	mmc_dump_capabilities("host", mmc->host_caps);
#endif

	if (mmc_host_is_spi(mmc)) {
		mmc_set_bus_width(mmc, 1);
		mmc_select_mode(mmc, SD_LEGACY);
		mmc_set_clock(mmc, mmc->tran_speed, MMC_CLK_ENABLE);
		return 0;
	}

	/* Restrict card's capabilities by what the host can do */
	caps = card_caps & mmc->host_caps;

	if (!uhs_en)
		caps &= ~UHS_CAPS;

	for_each_sd_mode_by_pref(caps, mwt) {
		uint *w;

		for (w = widths; w < widths + ARRAY_SIZE(widths); w++) {
			if (*w & caps & mwt->widths) {
				log_info(&log, "trying mode %s width %d (at %d MHz)\n",
					 mmc_mode_name(mwt->mode),
					 bus_width(*w),
					 mmc_mode2freq(mmc, mwt->mode) / 1000000);

				/* configure the bus width (card + host) */
				err = sd_select_bus_width(mmc, bus_width(*w));
				if (err)
					goto error;
				mmc_set_bus_width(mmc, bus_width(*w));

				/* configure the bus mode (card) */
				err = sd_set_card_speed(mmc, mwt->mode);
				if (err)
					goto error;

				/* configure the bus mode (host) */
				mmc_select_mode(mmc, mwt->mode);
				mmc_set_clock(mmc, mmc->tran_speed,
						MMC_CLK_ENABLE);

				err = sd_read_ssr(mmc);
				if (err)
					log_info(&log, "unable to read ssr\n");
				if (!err)
					return 0;

error:
				/* revert to a safer bus speed */
				mmc_select_mode(mmc, SD_LEGACY);
				mmc_set_clock(mmc, mmc->tran_speed,
						MMC_CLK_ENABLE);
			}
		}
	}

	log_warn(&log, "unable to select a mode\n");
	return -ENOTSUP;
}

/*
 * read the compare the part of ext csd that is constant.
 * This can be used to check that the transfer is working
 * as expected.
 */
static int mmc_read_and_compare_ext_csd(struct mmc *mmc)
{
	int err;
	const u8 *ext_csd = mmc->ext_csd;
	u8 test_csd[MMC_MAX_BLOCK_LEN] __attribute__ ((aligned(64)));

	if (mmc->version < MMC_VERSION_4)
		return 0;

	err = mmc_send_ext_csd(mmc, test_csd);
	if (err)
		return err;

	/* Only compare read only fields */
	if (ext_csd[EXT_CSD_PARTITIONING_SUPPORT]
		== test_csd[EXT_CSD_PARTITIONING_SUPPORT] &&
	    ext_csd[EXT_CSD_HC_WP_GRP_SIZE]
		== test_csd[EXT_CSD_HC_WP_GRP_SIZE] &&
	    ext_csd[EXT_CSD_REV]
		== test_csd[EXT_CSD_REV] &&
	    ext_csd[EXT_CSD_HC_ERASE_GRP_SIZE]
		== test_csd[EXT_CSD_HC_ERASE_GRP_SIZE] &&
	    memcmp(&ext_csd[EXT_CSD_SEC_CNT],
		   &test_csd[EXT_CSD_SEC_CNT], 4) == 0)
		return 0;

	return -EBADMSG;
}

static inline int mmc_set_lowest_voltage(struct mmc *mmc, enum bus_mode mode,
					 uint32_t allowed_mask)
{
	return 0;
}

static const struct mode_width_tuning mmc_modes_by_pref[] = {
	{
		.mode = MMC_DDR_52,
		.widths = MMC_MODE_8BIT | MMC_MODE_4BIT,
	},
	{
		.mode = MMC_HS_52,
		.widths = MMC_MODE_8BIT | MMC_MODE_4BIT | MMC_MODE_1BIT,
	},
	{
		.mode = MMC_HS,
		.widths = MMC_MODE_8BIT | MMC_MODE_4BIT | MMC_MODE_1BIT,
	},
	{
		.mode = MMC_LEGACY,
		.widths = MMC_MODE_8BIT | MMC_MODE_4BIT | MMC_MODE_1BIT,
	}
};

#define for_each_mmc_mode_by_pref(caps, mwt) \
	for (mwt = mmc_modes_by_pref;\
	    mwt < mmc_modes_by_pref + ARRAY_SIZE(mmc_modes_by_pref);\
	    mwt++) \
		if (caps & MMC_CAP(mwt->mode))

static const struct ext_csd_bus_width {
	uint cap;
	bool is_ddr;
	uint ext_csd_bits;
} ext_csd_bus_width[] = {
	{MMC_MODE_8BIT, true, EXT_CSD_DDR_BUS_WIDTH_8},
	{MMC_MODE_4BIT, true, EXT_CSD_DDR_BUS_WIDTH_4},
	{MMC_MODE_8BIT, false, EXT_CSD_BUS_WIDTH_8},
	{MMC_MODE_4BIT, false, EXT_CSD_BUS_WIDTH_4},
	{MMC_MODE_1BIT, false, EXT_CSD_BUS_WIDTH_1},
};

static int mmc_select_hs400(struct mmc *mmc)
{
	return -ENOTSUP;
}

static int mmc_select_hs400es(struct mmc *mmc)
{
	return -ENOTSUP;
}

#define for_each_supported_width(caps, ddr, ecbv) \
	for (ecbv = ext_csd_bus_width;\
	    ecbv < ext_csd_bus_width + ARRAY_SIZE(ext_csd_bus_width);\
	    ecbv++) \
		if ((ddr == ecbv->is_ddr) && (caps & ecbv->cap))

static int mmc_select_mode_and_width(struct mmc *mmc, uint card_caps)
{
	int err;
	const struct mode_width_tuning *mwt;
	const struct ext_csd_bus_width *ecbw;

#ifdef DEBUG
	mmc_dump_capabilities("mmc", card_caps);
	mmc_dump_capabilities("host", mmc->host_caps);
#endif

	if (mmc_host_is_spi(mmc)) {
		mmc_set_bus_width(mmc, 1);
		mmc_select_mode(mmc, MMC_LEGACY);
		mmc_set_clock(mmc, mmc->tran_speed, MMC_CLK_ENABLE);
		return 0;
	}

	/* Restrict card's capabilities by what the host can do */
	card_caps &= mmc->host_caps;

	/* Only version 4 of MMC supports wider bus widths */
	if (mmc->version < MMC_VERSION_4)
		return 0;

	if (!mmc->ext_csd) {
		log_warn(&log, "No ext_csd found!\n"); /* this should enver happen */
		return -ENOTSUP;
	}

	mmc_set_clock(mmc, mmc->legacy_speed, MMC_CLK_ENABLE);

	for_each_mmc_mode_by_pref(card_caps, mwt) {
		for_each_supported_width(card_caps & mwt->widths,
					 mmc_is_mode_ddr(mwt->mode), ecbw) {
			enum mmc_voltage old_voltage;
			log_warn(&log, "trying mode %s width %d (at %d MHz)\n",
				 mmc_mode_name(mwt->mode),
				 bus_width(ecbw->cap),
				 mmc_mode2freq(mmc, mwt->mode) / 1000000);
			old_voltage = mmc->signal_voltage;
			err = mmc_set_lowest_voltage(mmc, mwt->mode,
						     MMC_ALL_SIGNAL_VOLTAGE);
			if (err)
				continue;

			/* configure the bus width (card + host) */
			err = mmc_switch(mmc, EXT_CSD_CMD_SET_NORMAL,
				    EXT_CSD_BUS_WIDTH,
				    ecbw->ext_csd_bits & ~EXT_CSD_DDR_FLAG);
			if (err)
				goto error;
			mmc_set_bus_width(mmc, bus_width(ecbw->cap));

			if (mwt->mode == MMC_HS_400) {
				err = mmc_select_hs400(mmc);
				if (err) {
					log_info(&log, "Select HS400 failed %d\n", err);
					goto error;
				}
			} else if (mwt->mode == MMC_HS_400_ES) {
				err = mmc_select_hs400es(mmc);
				if (err) {
					log_info(&log, "Select HS400ES failed %d\n",
					       err);
					goto error;
				}
			} else {
				/* configure the bus speed (card) */
				err = mmc_set_card_speed(mmc, mwt->mode, false);
				if (err)
					goto error;

				/*
				 * configure the bus width AND the ddr mode
				 * (card). The host side will be taken care
				 * of in the next step
				 */
				if (ecbw->ext_csd_bits & EXT_CSD_DDR_FLAG) {
					err = mmc_switch(mmc,
							 EXT_CSD_CMD_SET_NORMAL,
							 EXT_CSD_BUS_WIDTH,
							 ecbw->ext_csd_bits);
					if (err)
						goto error;
				}

				/* configure the bus mode (host) */
				mmc_select_mode(mmc, mwt->mode);
				mmc_set_clock(mmc, mmc->tran_speed,
					      MMC_CLK_ENABLE);
			}

			/* do a transfer to check the configuration */
			err = mmc_read_and_compare_ext_csd(mmc);
			if (!err)
				return 0;
error:
			mmc_set_signal_voltage(mmc, old_voltage);
			/* if an error occured, revert to a safer bus mode */
			mmc_switch(mmc, EXT_CSD_CMD_SET_NORMAL,
				   EXT_CSD_BUS_WIDTH, EXT_CSD_BUS_WIDTH_1);
			mmc_select_mode(mmc, MMC_LEGACY);
			mmc_set_bus_width(mmc, 1);
		}
	}

	log_warn(&log, "unable to select a mode\n");

	return -ENOTSUP;
}

static int mmc_startup_v4(struct mmc *mmc)
{
	int err, i;
	u64 capacity;
	bool has_parts = false;
	bool part_completed;
	static const u32 mmc_versions[] = {
		MMC_VERSION_4,
		MMC_VERSION_4_1,
		MMC_VERSION_4_2,
		MMC_VERSION_4_3,
		MMC_VERSION_4_4,
		MMC_VERSION_4_41,
		MMC_VERSION_4_5,
		MMC_VERSION_5_0,
		MMC_VERSION_5_1
	};

	u8 ext_csd[MMC_MAX_BLOCK_LEN] __attribute__ ((aligned(64)));

	if (IS_SD(mmc) || (mmc->version < MMC_VERSION_4))
		return 0;

	/* check  ext_csd version and capacity */
	err = mmc_send_ext_csd(mmc, ext_csd);
	if (err)
		goto error;

	/* store the ext csd for future reference */
	if (!mmc->ext_csd)
		mmc->ext_csd = malloc(MMC_MAX_BLOCK_LEN);
	if (!mmc->ext_csd)
		return -ENOMEM;
	memcpy(mmc->ext_csd, ext_csd, MMC_MAX_BLOCK_LEN);
	if (ext_csd[EXT_CSD_REV] >= ARRAY_SIZE(mmc_versions))
		return -EINVAL;

	mmc->version = mmc_versions[ext_csd[EXT_CSD_REV]];

	if (mmc->version >= MMC_VERSION_4_2) {
		/*
		 * According to the JEDEC Standard, the value of
		 * ext_csd's capacity is valid if the value is more
		 * than 2GB
		 */
		capacity = ext_csd[EXT_CSD_SEC_CNT] << 0
				| ext_csd[EXT_CSD_SEC_CNT + 1] << 8
				| ext_csd[EXT_CSD_SEC_CNT + 2] << 16
				| ext_csd[EXT_CSD_SEC_CNT + 3] << 24;
		capacity *= MMC_MAX_BLOCK_LEN;
		if ((capacity >> 20) > 2 * 1024)
			mmc->capacity_user = capacity;
	}

	if (mmc->version >= MMC_VERSION_4_5)
		mmc->gen_cmd6_time = ext_csd[EXT_CSD_GENERIC_CMD6_TIME];

	/* The partition data may be non-zero but it is only
	 * effective if PARTITION_SETTING_COMPLETED is set in
	 * EXT_CSD, so ignore any data if this bit is not set,
	 * except for enabling the high-capacity group size
	 * definition (see below).
	 */
	part_completed = !!(ext_csd[EXT_CSD_PARTITION_SETTING] &
			    EXT_CSD_PARTITION_SETTING_COMPLETED);

	mmc->part_switch_time = ext_csd[EXT_CSD_PART_SWITCH_TIME];
	/* Some eMMC set the value too low so set a minimum */
	if (mmc->part_switch_time < MMC_MIN_PART_SWITCH_TIME && mmc->part_switch_time)
		mmc->part_switch_time = MMC_MIN_PART_SWITCH_TIME;

	/* store the partition info of emmc */
	mmc->part_support = ext_csd[EXT_CSD_PARTITIONING_SUPPORT];
	if ((ext_csd[EXT_CSD_PARTITIONING_SUPPORT] & PART_SUPPORT) ||
	    ext_csd[EXT_CSD_BOOT_MULT])
		mmc->part_config = ext_csd[EXT_CSD_PART_CONF];
	if (part_completed &&
	    (ext_csd[EXT_CSD_PARTITIONING_SUPPORT] & ENHNCD_SUPPORT))
		mmc->part_attr = ext_csd[EXT_CSD_PARTITIONS_ATTRIBUTE];

	mmc->capacity_boot = ext_csd[EXT_CSD_BOOT_MULT] << 17;

	mmc->capacity_rpmb = ext_csd[EXT_CSD_RPMB_MULT] << 17;

	for (i = 0; i < 4; i++) {
		int idx = EXT_CSD_GP_SIZE_MULT + i * 3;
		uint mult = (ext_csd[idx + 2] << 16) +
			(ext_csd[idx + 1] << 8) + ext_csd[idx];
		if (mult)
			has_parts = true;
		if (!part_completed)
			continue;
		mmc->capacity_gp[i] = mult;
		mmc->capacity_gp[i] *=
			ext_csd[EXT_CSD_HC_ERASE_GRP_SIZE];
		mmc->capacity_gp[i] *= ext_csd[EXT_CSD_HC_WP_GRP_SIZE];
		mmc->capacity_gp[i] <<= 19;
	}

	if (part_completed) {
		mmc->enh_user_size =
			(ext_csd[EXT_CSD_ENH_SIZE_MULT + 2] << 16) +
			(ext_csd[EXT_CSD_ENH_SIZE_MULT + 1] << 8) +
			ext_csd[EXT_CSD_ENH_SIZE_MULT];
		mmc->enh_user_size *= ext_csd[EXT_CSD_HC_ERASE_GRP_SIZE];
		mmc->enh_user_size *= ext_csd[EXT_CSD_HC_WP_GRP_SIZE];
		mmc->enh_user_size <<= 19;
		mmc->enh_user_start =
			(ext_csd[EXT_CSD_ENH_START_ADDR + 3] << 24) +
			(ext_csd[EXT_CSD_ENH_START_ADDR + 2] << 16) +
			(ext_csd[EXT_CSD_ENH_START_ADDR + 1] << 8) +
			ext_csd[EXT_CSD_ENH_START_ADDR];
		if (mmc->high_capacity)
			mmc->enh_user_start <<= 9;
	}

	/*
	 * Host needs to enable ERASE_GRP_DEF bit if device is
	 * partitioned. This bit will be lost every time after a reset
	 * or power off. This will affect erase size.
	 */
	if (part_completed)
		has_parts = true;
	if ((ext_csd[EXT_CSD_PARTITIONING_SUPPORT] & PART_SUPPORT) &&
	    (ext_csd[EXT_CSD_PARTITIONS_ATTRIBUTE] & PART_ENH_ATTRIB))
		has_parts = true;
	if (has_parts) {
		err = mmc_switch(mmc, EXT_CSD_CMD_SET_NORMAL,
				 EXT_CSD_ERASE_GROUP_DEF, 1);

		if (err)
			goto error;

		ext_csd[EXT_CSD_ERASE_GROUP_DEF] = 1;
	}

	if (ext_csd[EXT_CSD_ERASE_GROUP_DEF] & 0x01) {
		/* Read out group size from ext_csd */
		mmc->erase_grp_size =
			ext_csd[EXT_CSD_HC_ERASE_GRP_SIZE] * 1024;
		/*
		 * if high capacity and partition setting completed
		 * SEC_COUNT is valid even if it is smaller than 2 GiB
		 * JEDEC Standard JESD84-B45, 6.2.4
		 */
		if (mmc->high_capacity && part_completed) {
			capacity = (ext_csd[EXT_CSD_SEC_CNT]) |
				(ext_csd[EXT_CSD_SEC_CNT + 1] << 8) |
				(ext_csd[EXT_CSD_SEC_CNT + 2] << 16) |
				(ext_csd[EXT_CSD_SEC_CNT + 3] << 24);
			capacity *= MMC_MAX_BLOCK_LEN;
			mmc->capacity_user = capacity;
		}
	}
	else {
		/* Calculate the group size from the csd value. */
		int erase_gsz, erase_gmul;

		erase_gsz = (mmc->csd[2] & 0x00007c00) >> 10;
		erase_gmul = (mmc->csd[2] & 0x000003e0) >> 5;
		mmc->erase_grp_size = (erase_gsz + 1)
			* (erase_gmul + 1);
	}
	mmc->hc_wp_grp_size = 1024
		* ext_csd[EXT_CSD_HC_ERASE_GRP_SIZE]
		* ext_csd[EXT_CSD_HC_WP_GRP_SIZE];

	mmc->wr_rel_set = ext_csd[EXT_CSD_WR_REL_SET];

	return 0;
error:
	if (mmc->ext_csd) {
		free(mmc->ext_csd);
		mmc->ext_csd = NULL;
	}
	return err;
}

static int mmc_startup(struct mmc *mmc)
{
	int err, i;
	uint mult, freq;
	u64 cmult, csize;
	struct mmc_cmd cmd;
	struct blk_desc *bdesc;

	/* Put the Card in Identify Mode */
	cmd.cmdidx = mmc_host_is_spi(mmc) ? MMC_CMD_SEND_CID :
		MMC_CMD_ALL_SEND_CID; /* cmd not supported in spi */
	cmd.resp_type = MMC_RSP_R2;
	cmd.cmdarg = 0;

	err = mmc_send_cmd(mmc, &cmd, NULL);

	if (err && (mmc->quirks & MMC_QUIRK_RETRY_SEND_CID)) {
		int retries = 4;
		/*
		 * It has been seen that SEND_CID may fail on the first
		 * attempt, let's try a few more time
		 */
		do {
			err = mmc_send_cmd(mmc, &cmd, NULL);
			if (!err)
				break;
		} while (retries--);
	}

	if (err)
		return err;

	memcpy(mmc->cid, cmd.response, 16);

	/*
	 * For MMC cards, set the Relative Address.
	 * For SD cards, get the Relatvie Address.
	 * This also puts the cards into Standby State
	 */
	if (!mmc_host_is_spi(mmc)) { /* cmd not supported in spi */
		cmd.cmdidx = SD_CMD_SEND_RELATIVE_ADDR;
		cmd.cmdarg = mmc->rca << 16;
		cmd.resp_type = MMC_RSP_R6;

		err = mmc_send_cmd(mmc, &cmd, NULL);

		if (err)
			return err;

		if (IS_SD(mmc))
			mmc->rca = (cmd.response[0] >> 16) & 0xffff;
	}

	/* Get the Card-Specific Data */
	cmd.cmdidx = MMC_CMD_SEND_CSD;
	cmd.resp_type = MMC_RSP_R2;
	cmd.cmdarg = mmc->rca << 16;

	err = mmc_send_cmd(mmc, &cmd, NULL);

	if (err)
		return err;

	mmc->csd[0] = cmd.response[0];
	mmc->csd[1] = cmd.response[1];
	mmc->csd[2] = cmd.response[2];
	mmc->csd[3] = cmd.response[3];

	if (mmc->version == MMC_VERSION_UNKNOWN) {
		int version = (cmd.response[0] >> 26) & 0xf;

		switch (version) {
		case 0:
			mmc->version = MMC_VERSION_1_2;
			break;
		case 1:
			mmc->version = MMC_VERSION_1_4;
			break;
		case 2:
			mmc->version = MMC_VERSION_2_2;
			break;
		case 3:
			mmc->version = MMC_VERSION_3;
			break;
		case 4:
			mmc->version = MMC_VERSION_4;
			break;
		default:
			mmc->version = MMC_VERSION_1_2;
			break;
		}
	}

	/* divide frequency by 10, since the mults are 10x bigger */
	freq = fbase[(cmd.response[0] & 0x7)];
	mult = multipliers[((cmd.response[0] >> 3) & 0xf)];

	mmc->legacy_speed = freq * mult;
	mmc_select_mode(mmc, MMC_LEGACY);

	mmc->dsr_imp = ((cmd.response[1] >> 12) & 0x1);
	mmc->read_bl_len = 1 << ((cmd.response[1] >> 16) & 0xf);

	if (IS_SD(mmc))
		mmc->write_bl_len = mmc->read_bl_len;
	else
		mmc->write_bl_len = 1 << ((cmd.response[3] >> 22) & 0xf);

	if (mmc->high_capacity) {
		csize = (mmc->csd[1] & 0x3f) << 16
			| (mmc->csd[2] & 0xffff0000) >> 16;
		cmult = 8;
	} else {
		csize = (mmc->csd[1] & 0x3ff) << 2
			| (mmc->csd[2] & 0xc0000000) >> 30;
		cmult = (mmc->csd[2] & 0x00038000) >> 15;
	}

	mmc->capacity_user = (csize + 1) << (cmult + 2);
	mmc->capacity_user *= mmc->read_bl_len;
	mmc->capacity_boot = 0;
	mmc->capacity_rpmb = 0;
	for (i = 0; i < 4; i++)
		mmc->capacity_gp[i] = 0;

	if (mmc->read_bl_len > MMC_MAX_BLOCK_LEN)
		mmc->read_bl_len = MMC_MAX_BLOCK_LEN;

	if (mmc->write_bl_len > MMC_MAX_BLOCK_LEN)
		mmc->write_bl_len = MMC_MAX_BLOCK_LEN;

	if ((mmc->dsr_imp) && (0xffffffff != mmc->dsr)) {
		cmd.cmdidx = MMC_CMD_SET_DSR;
		cmd.cmdarg = (mmc->dsr & 0xffff) << 16;
		cmd.resp_type = MMC_RSP_NONE;
		if (mmc_send_cmd(mmc, &cmd, NULL))
			log_warn(&log, "MMC: SET_DSR failed\n");
	}

	/* Select the card, and put it into Transfer Mode */
	if (!mmc_host_is_spi(mmc)) { /* cmd not supported in spi */
		cmd.cmdidx = MMC_CMD_SELECT_CARD;
		cmd.resp_type = MMC_RSP_R1;
		cmd.cmdarg = mmc->rca << 16;
		err = mmc_send_cmd(mmc, &cmd, NULL);

		if (err)
			return err;
	}

	/*
	 * For SD, its erase group is always one sector
	 */
	mmc->erase_grp_size = 1;
	mmc->part_config = MMCPART_NOAVAILABLE;

	err = mmc_startup_v4(mmc);
	if (err)
		return err;

	err = mmc_set_capacity(mmc, bcm2835_get_part());
	if (err)
		return err;

	if (IS_SD(mmc)) {
		err = sd_get_capabilities(mmc);
		if (err)
			return err;
		err = sd_select_mode_and_width(mmc, mmc->card_caps);
	} else {
		err = mmc_get_capabilities(mmc);
		if (err)
			return err;
		mmc_select_mode_and_width(mmc, mmc->card_caps);
	}
	if (err)
		return err;

	mmc->best_mode = mmc->selected_mode;

	/* Fix the block length for DDR mode */
	if (mmc->ddr_mode) {
		mmc->read_bl_len = MMC_MAX_BLOCK_LEN;
		mmc->write_bl_len = MMC_MAX_BLOCK_LEN;
	}

	snprintf(bcm2835_get_vendor(), BLK_VEN_SIZE + 1, "Man %06x Snr %04x%04x",
		mmc->cid[0] >> 24, (mmc->cid[2] & 0xffff),
		(mmc->cid[3] >> 16) & 0xffff);
	snprintf(bcm2835_get_product(), BLK_PRD_SIZE + 1, "%c%c%c%c%c%c", mmc->cid[0] & 0xff,
		(mmc->cid[1] >> 24), (mmc->cid[1] >> 16) & 0xff,
		(mmc->cid[1] >> 8) & 0xff, mmc->cid[1] & 0xff,
		(mmc->cid[2] >> 24) & 0xff);
	snprintf(bcm2835_get_revision(), BLK_REV_SIZE + 1, "%d.%d", (mmc->cid[2] >> 20) & 0xf,
		(mmc->cid[2] >> 16) & 0xf);

	return 0;
}

static int mmc_send_if_cond(struct mmc *mmc)
{
	struct mmc_cmd cmd;
	int err;

	cmd.cmdidx = SD_CMD_SEND_IF_COND;
	/* We set the bit if the host supports voltages between 2.7 and 3.6 V */
	cmd.cmdarg = ((mmc->cfg->voltages & 0xff8000) != 0) << 8 | 0xaa;
	cmd.resp_type = MMC_RSP_R7;

	err = mmc_send_cmd(mmc, &cmd, NULL);

	if (err)
		return err;

	if ((cmd.response[0] & 0xff) != 0xaa)
		return -EOPNOTSUPP;
	else
		mmc->version = SD_VERSION_2;

	return 0;
}

/* board-specific MMC power initializations. */
__weak void board_mmc_power_init(void)
{
}

static int mmc_power_init(struct mmc *mmc)
{
	power_on_sd_card();
	return 0;
}

/*
 * put the host in the initial state:
 * - turn on Vdd (card power supply)
 * - configure the bus width and clock to minimal values
 */
static void mmc_set_initial_state(struct mmc *mmc)
{
	int err;

	/* First try to set 3.3V. If it fails set to 1.8V */
	err = mmc_set_signal_voltage(mmc, MMC_SIGNAL_VOLTAGE_330);
	if (err != 0)
		err = mmc_set_signal_voltage(mmc, MMC_SIGNAL_VOLTAGE_180);
	if (err != 0)
		log_warn(&log, "mmc: failed to set signal voltage\n");

	mmc_select_mode(mmc, MMC_LEGACY);
	mmc_set_bus_width(mmc, 1);
	mmc_set_clock(mmc, 0, MMC_CLK_ENABLE);
}

static int mmc_power_on(struct mmc *mmc)
{
	return 0;
}

static int mmc_power_off(struct mmc *mmc)
{
	mmc_set_clock(mmc, 0, MMC_CLK_DISABLE);
	return 0;
}

static int mmc_power_cycle(struct mmc *mmc)
{
	int ret;

	ret = mmc_power_off(mmc);
	if (ret)
		return ret;

	ret = mmc_host_power_cycle(mmc);
	if (ret)
		return ret;

	/*
	 * SD spec recommends at least 1ms of delay. Let's wait for 2ms
	 * to be on the safer side.
	 */
	udelay(2000);
	return mmc_power_on(mmc);
}

int mmc_get_op_cond(struct mmc *mmc)
{
	bool uhs_en = supports_uhs(mmc->cfg->host_caps);
	int err;

	if (mmc->has_init)
		return 0;

	err = mmc_power_init(mmc);
	if (err)
		return err;

	mmc->quirks = MMC_QUIRK_RETRY_SET_BLOCKLEN |
		      MMC_QUIRK_RETRY_SEND_CID |
		      MMC_QUIRK_RETRY_APP_CMD;

	err = mmc_power_cycle(mmc);
	if (err) {
		/*
		 * if power cycling is not supported, we should not try
		 * to use the UHS modes, because we wouldn't be able to
		 * recover from an error during the UHS initialization.
		 */
		log_warn(&log, "Unable to do a full power cycle. Disabling the UHS modes for safety\n");
		uhs_en = false;
		mmc->host_caps &= ~UHS_CAPS;
		err = mmc_power_on(mmc);
	}
	if (err)
		return err;

	/* The device has already been probed ready for use */
	mmc->ddr_mode = 0;

retry:
	mmc_set_initial_state(mmc);

	/* Reset the Card */
	err = mmc_go_idle(mmc);

	if (err)
		return err;

	/* Test for SD version 2 */
	err = mmc_send_if_cond(mmc);

	/* Now try to get the SD card's operating condition */
	err = sd_send_op_cond(mmc, uhs_en);
	if (err && uhs_en) {
		uhs_en = false;
		mmc_power_cycle(mmc);
		goto retry;
	}

	/* If the command timed out, we check for an MMC card */
	if (err == -ETIMEDOUT) {
		err = mmc_send_op_cond(mmc);

		if (err) {
			log_warn(&log, "Card did not respond to voltage select!\n");
			return -EOPNOTSUPP;
		}
	}

	return err;
}

int mmc_start_init(struct mmc *mmc)
{
	bool no_card;
	int err = 0;

	/*
	 * all hosts are capable of 1 bit bus-width and able to use the legacy
	 * timings.
	 */
	mmc->host_caps = mmc->cfg->host_caps | MMC_CAP(SD_LEGACY) |
			 MMC_CAP(MMC_LEGACY) | MMC_MODE_1BIT;

	no_card = mmc_getcd(mmc) == 0;
	if (no_card) {
		mmc->has_init = 0;
		log_warn(&log, "MMC: no card present\n");
		return -ENODEV;
	}

	err = mmc_get_op_cond(mmc);

	if (!err)
		mmc->init_in_progress = 1;

	return err;
}

static int mmc_complete_init(struct mmc *mmc)
{
	int err = 0;

	mmc->init_in_progress = 0;
	if (mmc->op_cond_pending)
		err = mmc_complete_op_cond(mmc);

	if (!err)
		err = mmc_startup(mmc);
	if (err)
		mmc->has_init = 0;
	else
		mmc->has_init = 1;
	return err;
}

int mmc_init(struct mmc *mmc)
{
	int err = 0;
	u32_t start, end;

	if (mmc->has_init)
		return 0;

	read_frclock(&start);

	if (!mmc->init_in_progress)
		err = mmc_start_init(mmc);

	if (!err)
		err = mmc_complete_init(mmc);
	if (err) {
		read_frclock(&end);
		log_info(&log, "%s: %d, time %lu\n", __func__, err, delta_frclock(start, end));
	}

	return err;
}

int mmc_deinit(struct mmc *mmc)
{
	u32 caps_filtered;

	if (!mmc->has_init)
		return 0;

	if (IS_SD(mmc)) {
		caps_filtered = mmc->card_caps &
			~(MMC_CAP(UHS_SDR12) | MMC_CAP(UHS_SDR25) |
			  MMC_CAP(UHS_SDR50) | MMC_CAP(UHS_DDR50) |
			  MMC_CAP(UHS_SDR104));

		return sd_select_mode_and_width(mmc, caps_filtered);
	} else {
		caps_filtered = mmc->card_caps &
			~(MMC_CAP(MMC_HS_200) | MMC_CAP(MMC_HS_400));

		return mmc_select_mode_and_width(mmc, caps_filtered);
	}
}

int mmc_set_dsr(struct mmc *mmc, u16 val)
{
	mmc->dsr = val;
	return 0;
}

void mmc_set_preinit(struct mmc *mmc, int preinit)
{
	mmc->preinit = preinit;
}
