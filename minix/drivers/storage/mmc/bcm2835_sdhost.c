/*
*
* bcm2835_sdhost.c
*
* This is a sd card device driver for raspberry pi2/3 under minix.
*
* This code was ported from the U-boot RPI device driver 
* dirvers/mmc/bcm2835_sdhost.c by steven stewart <sstewart6@yahoo.com>
*
*/
#include <stdlib.h>
#include <stdbool.h>
#include <minix/blockdriver.h>
#include <minix/type.h>
#include <minix/log.h>
#include <minix/mmio.h>
#include <sys/mman.h>
#include <sys/errno.h>
#include <assert.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include "mmchost.h"
#include <minix/ipc.h>
#include <minix/com.h>
#include <minix/board.h>
#include "bcm2835_sdhost.h"
#include <minix/sysutil.h>

#define dsb() __asm__ __volatile__ ("dsb sy\n" : : : "memory")
#define mb() __asm__ __volatile__ ("dmb 3\n" : : : "memory")
#define rmb() __asm__ __volatile__ ("dmb 1\n" : : : "memory")
#define wmb() __asm__ __volatile__ ("dmb 2\n" : : : "memory")

#define likley(x) __builtin_expect((x),1)
#define unlikely(x) __builtin_expect((x),0)

#define WARN_ON(condition) ({			\
	int __ret_warn_on = !!(condition);	\
	if (unlikely(__ret_warn_on))		\
		printf("WARNING at %s:%d/%s()!\n", __FILE__, __LINE__, __func__); \
	unlikely(__ret_warn_on);	\
})

#define be32_to_le(x) \
	((u32_t)( \
	(((u32_t)(x) & (u32_t)0x000000ffUL) << 24) | \
	(((u32_t)(x) & (u32_t)0x0000ff00UL) <<  8) | \
	(((u32_t)(x) & (u32_t)0x00ff0000UL) >>  8) | \
	(((u32_t)(x) & (u32_t)0xff000000UL) >> 24) ))

#define min(X, Y) \
	({ typeof(X) __x = (X); \
	typeof(Y) __y = (Y); \
	(__x < __y) ? __x : __y; })

#define max(X, Y) \
	({ typeof(X) __x = (X); \
	typeof(Y) __y = (Y); \
	(__x > __y) ? __x : __y; })

int micro_delay(u32_t);

static int hook_id = 1;

static struct log log = {
	.name = "mmc_host_mmchs",
	.log_level = LEVEL_INFO,
	.log_func = default_log
};

struct mmc_cmd {
	u16_t cmd;
	u32_t args;
	u32_t resp_type;
	u32_t resp[4];
};

struct mmc_data {
	union {
		char *dest;
		const char *src;
	};
	u32_t flags;
	u32_t blocks;
	u32_t blocksize;
};

struct bcm2835_sdhost_registers {
	/* SD command to card */
	vir_bytes SDCMD;
	/* SD argument to card */
	vir_bytes SDARG;
	/* Start value for timeout counter */
	vir_bytes SDTOUT;
	/* Start value for clock divider */
	vir_bytes SDCDIV;
	/* SD card response (31:0) */
	vir_bytes SDRSP0;
	/* SD card response (63:32) */
	vir_bytes SDRSP1;
	/* SD card response (95:64) */
	vir_bytes SDRSP2;
	/* SD card response (127:96) */
	vir_bytes SDRSP3;
	/* SD host status */
	vir_bytes SDHSTS;
	/* SD card power control */
	vir_bytes SDVDD;
	/* Emergency debug mode */
	vir_bytes SDEDM;
	/* Host configuration  */
	vir_bytes SDHCFG;
	/* Host byte count (debug) */
	vir_bytes SDHBCT;
	/* Data to/from SD card  */
	vir_bytes SDDATA;
	/* Host block count (SDIO/SDHC) */
	vir_bytes SDHBLC;
};

static struct bcm2835_sdhost_registers bcm2835_regs = {
	.SDCMD = 0x0,
	.SDARG = 0x4,
	.SDTOUT = 0x8,
	.SDCDIV = 0xc,
	.SDRSP0 = 0x10,
	.SDRSP1 = 0x14,
	.SDRSP2 = 0x18,
	.SDRSP3 = 0x1c,
	.SDHSTS = 0x20,
	.SDVDD = 0x30,
	.SDEDM = 0x34,
	.SDHCFG = 0x38,
	.SDHBCT = 0x3c,
	.SDDATA = 0x40,
	.SDHBLC = 0x50,
};

struct bcm2835_sdhost {
	vir_bytes io_base;
	vir_bytes io_size;
	phys_bytes hw_base; /* HW address */
	int irq_nr;
	struct bcm2835_sdhost_registers *regs;
	struct mmc_cmd *cmd;
	struct mmc_data *data;
	u32_t clock;
	u32_t max_clock;
	u32_t sectors;
	u32_t hcfg;
	u32_t capabilities;
	u32_t bus_width;
	u32_t cdiv;
	u32_t voltages;
	u32_t version;
	u32_t alloc_unit;
	u32_t erase_timeout;
	u32_t erase_offset;
	u32_t high_capacity;
	u32_t blocks;
	u32_t ocr;
	bool uhs_enabled;
	bool use_busy;
};

struct bcm2835_sdhost bcm2835_mmchs = {
	.io_base = 0,
	.io_size = 0x100,
	.hw_base = 0x3f202000,
	.irq_nr = 86,
	.regs = &bcm2835_regs,
	.cmd = 0,
	.data = 0,
	.clock = 0,
	.max_clock = 250000000,
	.sectors = 0,
	.hcfg = 0,
	.capabilities = 0,
	.bus_width = 1,
	.cdiv = 0,
	.voltages = 0,
	.version = 0,
	.alloc_unit = 0,
	.erase_timeout = 0,
	.erase_offset = 0,
	.high_capacity = 0,
	.blocks = 0,
	.ocr = 0,
	.uhs_enabled = true,
	.use_busy = true,
};

struct bcm2835_sdhost *mmchs;

static u32_t edm_fifo_fill(u32_t edm) {
	return (edm >> SDEDM_FIFO_FILL_SHIFT) & SDEDM_FIFO_FILL_MASK;
}

static inline u32_t mmc_read32(const volatile u32_t addr) {
	u32_t w;

	__asm__ __volatile__("ldr %0, %1\n"
		: "=r" (w)
		: "m" (*(volatile u32_t *)addr)
		: "memory");
	rmb();
	return w;
}

static inline void mmc_write32(volatile u32_t addr, u32_t w) {
	wmb();
	__asm__ __volatile__("str %0, %1\n"
		:
		: "r" (w), "m" (*(volatile u32_t *)addr)
		: "memory");
}

static void bcm2835_dumpregs(struct bcm2835_sdhost *host) {
	log_warn(&log, "dumpregs\n");
	log_warn(&log, "SDCMD 0x%08x\n", mmc_read32(host->io_base + host->regs->SDCMD));
	log_warn(&log, "SDARG 0x%08x\n", mmc_read32(host->io_base + host->regs->SDARG));
	log_warn(&log, "SDTOUT 0x%08x\n", mmc_read32(host->io_base + host->regs->SDTOUT));
	log_warn(&log, "SDCDIV 0x%08x\n", mmc_read32(host->io_base + host->regs->SDCDIV));
	log_warn(&log, "SDRSP0 0x%08x\n", mmc_read32(host->io_base + host->regs->SDRSP0));
	log_warn(&log, "SDRSP1 0x%08x\n", mmc_read32(host->io_base + host->regs->SDRSP1));
	log_warn(&log, "SDRSP2 0x%08x\n", mmc_read32(host->io_base + host->regs->SDRSP2));
	log_warn(&log, "SDRSP3 0x%08x\n", mmc_read32(host->io_base + host->regs->SDRSP3));
	log_warn(&log, "SDHSTS 0x%08x\n", mmc_read32(host->io_base + host->regs->SDHSTS));
	log_warn(&log, "SDVDD 0x%08x\n", mmc_read32(host->io_base + host->regs->SDVDD));
	log_warn(&log, "SDEDM 0x%08x\n", mmc_read32(host->io_base + host->regs->SDEDM));
	log_warn(&log, "SDHCFG 0x%08x\n", mmc_read32(host->io_base + host->regs->SDHCFG));
	log_warn(&log, "SDHBCT 0x%08x\n", mmc_read32(host->io_base + host->regs->SDHBCT));
	log_warn(&log, "SDHBLC 0x%08x\n", mmc_read32(host->io_base + host->regs->SDHBLC));
	log_warn(&log, "\n");
}

int mmchs_host_set_instance(struct mmc_host *host, int instance) {

	log_info(&log, "Using instance number %d\n", instance);
	if(instance != 0)
		return EIO;
	
	return OK;
}

static void bcm2835_sdhost_reset_internal(struct bcm2835_sdhost *host) {
	u32_t sdedm;

	mmc_write32(host->io_base + host->regs->SDVDD, 0); /* power off */
	mmc_write32(host->io_base + host->regs->SDCMD, 0); 
	mmc_write32(host->io_base + host->regs->SDARG, 0); 
	mmc_write32(host->io_base + host->regs->SDTOUT, 0xf00000); 
	mmc_write32(host->io_base + host->regs->SDCDIV, 0); 
	mmc_write32(host->io_base + host->regs->SDHSTS, SDHSTS_CLEAR_MASK); 
	mmc_write32(host->io_base + host->regs->SDHCFG, 0); 
	mmc_write32(host->io_base + host->regs->SDHBCT, 0); 
	mmc_write32(host->io_base + host->regs->SDHBLC, 0); 

	sdedm = mmc_read32(host->io_base + host->regs->SDEDM);
	sdedm &= ~((SDEDM_THRESHOLD_MASK << SDEDM_READ_THRESHOLD_SHIFT) |
		(SDEDM_THRESHOLD_MASK << SDEDM_WRITE_THRESHOLD_SHIFT));
	sdedm |= (FIFO_READ_THRESHOLD << SDEDM_READ_THRESHOLD_SHIFT) |
		(FIFO_WRITE_THRESHOLD << SDEDM_WRITE_THRESHOLD_SHIFT);
	mmc_write32(host->io_base + host->regs->SDEDM, sdedm);
	micro_delay(20000);
	mmc_write32(host->io_base + host->regs->SDVDD, 1); /* power on */
	micro_delay(20000);
	host->clock = 0;
	host->sectors = 0;
	mmc_write32(host->io_base + host->regs->SDHCFG, host->hcfg);
	mmc_write32(host->io_base + host->regs->SDCDIV, 0);
}

static void sd_power_cycle(struct bcm2835_sdhost *host) {
	mmc_write32(host->io_base + host->regs->SDVDD, 0); /* power off */
	micro_delay(20000);
	mmc_write32(host->io_base + host->regs->SDVDD, 1); /* power on */
	micro_delay(20000);
}

static int bcm2835_wait_transfer_complete(struct bcm2835_sdhost *host) {
	int timediff = 0;

	while(1) {
		u32_t edm, fsm;

		edm = mmc_read32(host->io_base + host->regs->SDEDM);
		fsm = edm & SDEDM_FSM_MASK;

		if((fsm == SDEDM_FSM_IDENTMODE) ||
			(fsm == SDEDM_FSM_DATAMODE))
			break;

		if((fsm == SDEDM_FSM_READWAIT) ||
			(fsm == SDEDM_FSM_WRITESTART1) ||
			(fsm == SDEDM_FSM_READDATA)) {
			mmc_write32(host->io_base + host->regs->SDEDM, edm | SDEDM_FORCE_DATA_MODE);
			break;
		}

		if(timediff++ == 100000) {
			log_warn(&log, "wait_transfer_complete - still waiting after %d retries\n", timediff);
			return -ETIMEDOUT;
		}
	}

	return 0;
}

static u32_t bcm2835_read_wait_sdcmd(struct bcm2835_sdhost *host) {
	u32_t sdcmd;
	int rc;
	int timeout = SDHST_TIMEOUT_MAX_USEC;

	while(timeout--) {
		sdcmd = mmc_read32(host->io_base + host->regs->SDCMD);
		if(!(sdcmd & SDCMD_NEW_FLAG)) {
			break;
		}
		if(timeout == 0) {
			log_warn(&log, "%s: timeout (%d us)\n", __func__, timeout);
			return -ETIMEDOUT;
		}
		micro_delay(1);
	}

	return sdcmd;
}

static int bcm2835_finish_command(struct bcm2835_sdhost *host) {
	u32_t sdcmd;
	int rc = 0;

	sdcmd = bcm2835_read_wait_sdcmd(host);

	if(sdcmd & SDCMD_NEW_FLAG) {
		log_warn(&log, "command never completed.\n");
		return -EIO;
	}
	else {
		if(sdcmd & SDCMD_FAIL_FLAG) {
			bcm2835_dumpregs(host);
			u32_t sdhsts = mmc_read32(host->io_base + host->regs->SDHSTS);

			mmc_write32(host->io_base + host->regs->SDHSTS, SDHSTS_ERROR_MASK);

			if(!(sdhsts & SDHSTS_CRC7_ERROR) ||
				(host->cmd->cmd != MMC_CMD_SEND_OP_COND)) {
					if(sdhsts & SDHSTS_CMD_TIME_OUT) {
						rc = -ETIMEDOUT;
					}
			}
			else {
				log_warn(&log, "unexpected command %d error\n", host->cmd->cmd);
				return -EILSEQ;
			}
			return rc;
		}
	}

	if(host->cmd->resp_type & MMC_RSP_PRESENT) {
		if(host->cmd->resp_type & MMC_RSP_136) {
			int i;
			
			for(i = 0;i < 4;i++) {
				host->cmd->resp[3 - i] = mmc_read32(host->io_base + host->regs->SDRSP0 + i * 4);
			}
		}
		else {
			host->cmd->resp[0] = mmc_read32(host->io_base + host->regs->SDRSP0);
		}
	}

	host->cmd = NULL;

	return rc;
}

static int bcm2835_transfer_block_pio(struct bcm2835_sdhost *host, bool is_read) {
	struct mmc_data *data = host->data;
	int block_size = data->blocksize;
	int copy_words;
	u32_t hsts = 0;
	u32_t *buf;

	if(block_size % sizeof(u32_t))
		return -EINVAL;

	buf = is_read ? (u32_t*) data->dest : (u32_t*) data->src;
	
	copy_words = block_size / sizeof(u32_t);

	while(copy_words) {
		int burst_words, words;
		u32_t edm;

		burst_words = min(SDDATA_FIFO_PIO_BURST, copy_words);
		edm = mmc_read32(host->io_base + host->regs->SDEDM);
		if(is_read)
			words = edm_fifo_fill(edm);
		else
			words = SDDATA_FIFO_WORDS - edm_fifo_fill(edm);
		
		if(words < burst_words) {
			int fsm_state = (edm & SDEDM_FSM_MASK);

			if((is_read && (fsm_state != SDEDM_FSM_READDATA &&
				fsm_state != SDEDM_FSM_READWAIT &&
				fsm_state != SDEDM_FSM_READCRC)) ||
				(!is_read && (fsm_state != SDEDM_FSM_WRITEDATA &&
					fsm_state != SDEDM_FSM_WRITEWAIT1 &&
					fsm_state != SDEDM_FSM_WRITEWAIT2 &&
					fsm_state != SDEDM_FSM_WRITECRC &&
					fsm_state != SDEDM_FSM_WRITESTART1 &&
					fsm_state != SDEDM_FSM_WRITESTART2))) {
				hsts = mmc_read32(host->io_base + host->regs->SDHSTS);
				log_warn(&log, "fsm %x, hsts %08x\n", fsm_state, hsts);
				if(hsts & SDHSTS_ERROR_MASK)
					break;
			}

			continue;
		}
		else {
			if(words > copy_words)
				words = copy_words;
		}

		copy_words -= words;

		while(words) {
			if(is_read)
				*(buf++) = mmc_read32(host->io_base + host->regs->SDDATA);
			else
				mmc_write32(host->io_base + host->regs->SDDATA, *(buf++));
			words--;
		}
	}

	return 0;
}

static int bcm2835_transfer_pio(struct bcm2835_sdhost *host) {
	u32_t sdhsts;
	bool is_read;
	int rc;

	is_read = (host->data->flags & MMC_DATA_READ) != 0;
	rc = bcm2835_transfer_block_pio(host, is_read);
	if(rc)
		return rc;

	sdhsts = mmc_read32(host->io_base + host->regs->SDHSTS);
	if(sdhsts & (SDHSTS_CRC16_ERROR | SDHSTS_CRC7_ERROR | SDHSTS_FIFO_ERROR)) {
		log_warn(&log, "%s transfer error - HSTS %08x\n", is_read ? "read" : "write", sdhsts);
		rc = -EILSEQ;
	}
	else {
		if((sdhsts & (SDHSTS_CMD_TIME_OUT | SDHSTS_REW_TIME_OUT))) {
			log_warn(&log, "timeout error - HSTS %08x\n", is_read ? "read" : "write", sdhsts);
			rc = -ETIMEDOUT; 
		}
	}

	return rc;
}

static void bcm2835_sdhost_prepare_data(struct bcm2835_sdhost *host, struct mmc_cmd *cmd, struct mmc_data *data) {

	WARN_ON(host->data);

	host->data = data;
	if(!data)
		return;

	host->blocks = data->blocks;

	mmc_write32(host->io_base + host->regs->SDHBCT, data->blocksize);
	mmc_write32(host->io_base + host->regs->SDHBLC, data->blocks);
}

static int bcm2835_check_cmd_error(struct bcm2835_sdhost *host, u32_t mask) {
	int rc = -EINVAL;

	if(!(mask & SDHSTS_ERROR_MASK))
		return 0;

	if(!host->cmd)
		return -EINVAL;

	log_warn(&log, "sdhost_busy_irq: intmask %08x\n", mask);

	if(mask & SDHSTS_CRC7_ERROR) {
		rc = -EILSEQ;
	}
	else {
		if(mask & (SDHSTS_CRC16_ERROR | SDHSTS_FIFO_ERROR)) {
			rc = -EILSEQ;
		}
		else {
			if(mask & (SDHSTS_REW_TIME_OUT | SDHSTS_CMD_TIME_OUT)) {
				rc = -ETIMEDOUT;
			}
		}
	}
	
	return rc;
}

static int bcm2835_check_data_error(struct bcm2835_sdhost *host, u32_t mask) {
	int rc = 0;
	
	if(!host->data)
		return 0;
	if(mask & (SDHSTS_CRC16_ERROR | SDHSTS_FIFO_ERROR))
		return -EILSEQ;
	if(mask & SDHSTS_REW_TIME_OUT)
		return -ETIMEDOUT;

	if(rc)
		log_warn(&log, "%s:%d %d\n", __func__, __LINE__, rc);

	return rc;
}

static int bcm2835_transmit(struct bcm2835_sdhost *host) {
	u32_t mask = mmc_read32(host->io_base + host->regs->SDHSTS);
	int rc;

	rc = bcm2835_check_data_error(host, mask);
	if(rc)
		return rc;

	rc = bcm2835_check_cmd_error(host, mask);
	if(rc)
		return rc;

	if(host->use_busy && (mask & SDHSTS_BUSY_IRPT)) {
		mmc_write32(host->io_base + host->regs->SDHSTS, SDHSTS_BUSY_IRPT);
		host->use_busy = false;
		bcm2835_finish_command(host);
	}

	if(host->data) {
		rc = bcm2835_transfer_pio(host);
		if(rc)
			return rc;
		host->blocks--;
		if(host->blocks == 0) {
			rc = bcm2835_wait_transfer_complete(host);
			if(rc)
				return rc;
			host->data = NULL;
		}
	}

	return 0;
}

static int bcm2835_sdhost_send_command(struct bcm2835_sdhost *host, struct mmc_cmd *cmd, struct mmc_data *data) {
	u32_t sdhsts, sdcmd;

	WARN_ON(host->cmd);
	if(host->cmd)
		log_warn(&log, "host cmd cmd %x\n", host->cmd->cmd);

	if((cmd->resp_type & MMC_RSP_136) && (cmd->resp_type & MMC_RSP_BUSY)) {
		log_warn(&log, "Unsupported response type\n");
		return -1;
	}

	sdcmd = bcm2835_read_wait_sdcmd(host);
	if(sdcmd & SDCMD_NEW_FLAG) {
		log_warn(&log, "MMC previous command never completed\n");
		bcm2835_dumpregs(host);
		return -EBUSY;
	}

	host->cmd = cmd;

	/* Clear any error flags */
	sdhsts = mmc_read32(host->io_base + host->regs->SDHSTS);
	if(sdhsts & SDHSTS_ERROR_MASK)
		mmc_write32(host->io_base + host->regs->SDHSTS, sdhsts);

	bcm2835_sdhost_prepare_data(host, cmd, data);

	sdcmd = cmd->cmd & SDCMD_CMD_MASK;

	mmc_write32(host->io_base + host->regs->SDARG, cmd->args);

	host->use_busy = false;

	if(!(cmd->resp_type & MMC_RSP_PRESENT)) {
		sdcmd |= SDCMD_NO_RESPONSE;
	}
	else {
		if(cmd->resp_type & MMC_RSP_136) {
			sdcmd |= SDCMD_LONG_RESPONSE;
		}
		if(cmd->resp_type & MMC_RSP_BUSY) {
			sdcmd |= SDCMD_BUSYWAIT;
		}
	}

	if(data) {
		if(data->flags & MMC_DATA_WRITE)
			sdcmd |= SDCMD_WRITE_CMD;
		if(data->flags & MMC_DATA_READ)
			sdcmd |= SDCMD_READ_CMD;
	}

	mmc_write32(host->io_base + host->regs->SDCMD, sdcmd | SDCMD_NEW_FLAG);

	return 0;
}

static inline int is_power_of_2(u32_t  x) {
	return !(x & (x - 1));
}

static int bcm2835_send_cmd(struct bcm2835_sdhost *host, struct mmc_cmd *cmd, struct mmc_data *data) {
	u32_t edm, fsm;
	int rc;

	if(data && !is_power_of_2(data->blocksize)) {
		log_warn(&log, "Unsupported block size %d\n", host->data->blocksize);

		if(cmd)
			return -1;
	}

	edm = mmc_read32(host->io_base + host->regs->SDEDM);
	fsm = edm & SDEDM_FSM_MASK;

	if((fsm != SDEDM_FSM_IDENTMODE) && 
		(fsm != SDEDM_FSM_DATAMODE) &&
		(cmd && cmd->cmd != MMC_CMD_STOP_TRANSMISSION)) {
		log_warn(&log, "Previous command %d did not complete EDM %0X8\n",
			mmc_read32(host->io_base + host->regs->SDCMD) & SDCMD_CMD_MASK, edm);

		if(cmd)
			return -1;

		return 0;
	}

	if(cmd) {
		rc = bcm2835_sdhost_send_command(host, cmd, data);
		if(!rc && !host->use_busy) {
			rc = bcm2835_finish_command(host);
		}
	}

	while(host->use_busy || host->data) {
		rc = bcm2835_transmit(host);
		if(rc)
			break;
	}

	return rc;
}

int bcm2835_send_status(struct bcm2835_sdhost *host, int timeout) {
	struct mmc_cmd cmd;
	int rc, retries = 5;

	cmd.cmd = MMC_CMD_SEND_STATUS;
	cmd.resp_type = MMC_RSP_R1; 

	while(1) {
		rc = bcm2835_send_cmd(host, &cmd, NULL);
		if(!rc) {
			if((cmd.resp[0] & MMC_STATUS_RDY_FOR_DATA) &&
				(cmd.resp[0] & MMC_STATUS_CURR_STATE) != MMC_STATE_PRG)
				break;

			if(cmd.resp[0] & MMC_STATUS_MASK) {
				return -ENOCONN;
			}
		}
		else
			if(--retries < 0)
				return rc;

		if(timeout-- <= 0)
			break;

		micro_delay(1000);
	}

	return 0;
}

static void bcm2835_sdhost_set_clock(u32_t clock) {
	int divider = 0;

	/* The SDCDIV register has 11 bits, and holds (div - 2).
		But in data mode the max is 50MHz wihout a minimum, and only the
		bottom 3 bits are used. Since the switch over is automatic (unless
		we have marked the card as slow...), chosen values have to make
		sense in both modes.
		Ident mode must be 100-400KHz, so can range check the requested
		clock. CMD15 must be used to return to data mode, so this can be
		monitored.

		clock 250MHz -> 0->125MHz, 1->83.3MHz, 2->62.5MHz, 3->50.0MHz
			4->41.7MHz, 5->35.7MHz, 6->31.3MHz, 7->27.8MHz

			623->400KHz/27.8MHz
			reset value (507)->491159/50MHz

		BUT, the 3-bit clock divisor in data mode is too small if the
		core clock is higher than 250MHz, so instead use the SLOW_CARD
		configuration bit to force the use of the ident clock divisor
		at all times.
	*/

	if(clock < 100000) {
		mmchs->cdiv = SDCDIV_MAX_CDIV;
		mmc_write32(mmchs->io_base + mmchs->regs->SDCDIV, mmchs->cdiv);
		return;
	}

	divider = mmchs->max_clock / clock;
	if(divider < 2)
		divider = 2;
	if((mmchs->max_clock / divider) > clock)
		divider++;
	divider -= 2;

	if(divider > SDCDIV_MAX_CDIV)
		divider = SDCDIV_MAX_CDIV;

	clock = mmchs->max_clock / (divider + 2);
	mmchs->clock = clock;
	mmchs->cdiv = divider;

	mmc_write32(mmchs->io_base + mmchs->regs->SDCDIV, mmchs->cdiv);
	mmc_write32(mmchs->io_base + mmchs->regs->SDTOUT, mmchs->clock / 2);
}

static void bcm2835_sdhost_set_ios(struct bcm2835_sdhost *host) {

	/* Set bus width */
	host->hcfg &= ~SDHCFG_WIDE_EXT_BUS;
	if(host->bus_width == 4)
		host->hcfg |= SDHCFG_WIDE_EXT_BUS;
	host->hcfg |= SDHCFG_WIDE_INT_BUS;

	/* Disable clever clock switching */
	host->hcfg |= SDHCFG_SLOW_CARD;

	mmc_write32(host->io_base + host->regs->SDHCFG, host->hcfg);
}

static int power_on_sd_card() {
	int mbox_fd, rc;
	u32_t buf[8];

	mbox_fd = open("/dev/mailbox", O_RDWR);

	if(mbox_fd < 0) {
		log_warn(&log, "ERROR opening /dev/mailbox\n");
		return mbox_fd;
	}

	buf[0] = 32;	/* total size */
	buf[1] = 0;		/* request */
	buf[2] = MBOX_SET_POWER;	/* tag */
	buf[3] = 8;		/* buffer size */
	buf[4] = 8;		/* request size */
	buf[5] = 3;		/* device id */
	buf[6] = 3;		/* state */	
	buf[7] = 0;		/* end tag */
	rc = write(mbox_fd, buf, 32);
	rc = read(mbox_fd, buf, 32);	

	if(!(buf[6] & 0x01)) {
		rc = 0;
		goto exit;
	}

	buf[0] = 32;
	buf[1] = 0;
	buf[2] = MBOX_SET_POWER; /* tag */
	buf[3] = 8;	/* buffer size */
	buf[4] = 8; /* request size */
	buf[5] = 0; /* device id */
	buf[6] = 3; /* state */
	buf[7] = 0; /* end tag */
	rc = write(mbox_fd, buf, 32);
	rc = read(mbox_fd, buf, 32);	

	if(!(buf[6] & 0x01)) {
		rc = 0;
		goto exit;
	}

	buf[0] = 32;
	buf[1] = 0;
	buf[2] = MBOX_GET_POWER; /* tag */
	buf[3] = 8;	/* buffer size */
	buf[4] = 4; /* request size */
	buf[5] = 0; /* device id */
	buf[6] = 0; /* state */
	buf[7] = 0; /* end tag */
	rc = write(mbox_fd, buf, 32);
	rc = read(mbox_fd, buf, 32);	

	buf[0] = 32;
	buf[1] = 0;
	buf[2] = MBOX_GET_CLOCK; /* tag */
	buf[3] = 8;	/* buffer size */
	buf[4] = 4; /* request size */
	buf[5] = 4; /* device id */
	buf[6] = 0; /* rate */
	buf[7] = 0; /* end tag */
	rc = write(mbox_fd, buf, 32);
	rc = read(mbox_fd, buf, 32);	
	
	mmchs->max_clock = buf[6];

	rc = 1;

exit:
	close(mbox_fd);
	return rc;
}

int mmchs_host_init(struct mmc_host *host) {
	struct minix_mem_range mem_range;
	int rc;
	message msg;
	struct machine machine;

	if(!power_on_sd_card()) {
		log_warn(&log, "Unable to power on SD card.\n");
		return 1;
	}
	/* Grant access to i/o memory */
	mem_range.mr_base = mmchs->hw_base;
	mem_range.mr_limit = mmchs->hw_base + mmchs->io_size;
	if(sys_privctl(SELF, SYS_PRIV_ADD_MEM, &mem_range) != 0)
		panic("Unable to request permission to map memory.");

	/* Save the virtual address to i/o mem */
	mmchs->io_base = (u32_t) vm_map_phys(SELF, (void*) mmchs->hw_base, mmchs->io_size);
	if(mmchs->io_base == (u32_t) MAP_FAILED)
		panic("unable to map MMC memory");

	mmchs->capabilities = MMC_MODE_4BIT | MMC_MODE_HS | MMC_MODE_HS_52MHz;
	mmchs->voltages = MMC_VDD_32_33 | MMC_VDD_33_34;
	mmchs->hcfg = SDHCFG_BUSY_IRPT_EN;

	bcm2835_sdhost_reset_internal(mmchs);

	rc = sys_irqsetpolicy(mmchs->irq_nr, 0, &hook_id);
	if(rc != OK) {
		log_warn(&log, "MMC: could not set IRQ policy rc %d irq %d\n", rc, mmchs->irq_nr);
		return 1;
	}
	
	return 0;
}

static int mmc_go_idle(struct bcm2835_sdhost *mmchs) {
	struct mmc_cmd cmd;
	int rc;

	micro_delay(1000);

	cmd.cmd = MMC_CMD_GO_IDLE_STATE;
	cmd.args = 0;
	cmd.resp_type = MMC_RSP_NONE;

	rc = bcm2835_send_cmd(mmchs, &cmd, NULL);

	if(rc)
		return rc;

	micro_delay(2000);

	return 0;
}

static int mmc_send_if_cond(struct bcm2835_sdhost *mmchs) {
	struct mmc_cmd cmd;
	int rc;
	static const unsigned char test_pattern = 0xaa;
	unsigned char result_pattern;

	cmd.cmd = SD_CMD_SEND_IF_COND;
	/* ocr bits [23:15] 1 1111 1111 2.7-3.6v and test pattern */
	cmd.args = ((mmchs->voltages & 0xff8000) != 0) << 8 | test_pattern;
	cmd.resp_type = MMC_RSP_R7;

	rc = bcm2835_send_cmd(mmchs, &cmd, NULL);

	if(rc)
		return rc;

	result_pattern = cmd.resp[0] & 0xff;

	if(result_pattern != test_pattern)
		return -1;

	mmchs->version = SD_VERSION_2;

	return 0;
}

static int sd_send_op_cond(struct bcm2835_sdhost *mmchs, struct sd_card_regs *card) {
	int timeout = 1000;
	int rc;
	struct mmc_cmd cmd;

	while(1) {
		cmd.cmd = MMC_CMD_APP_CMD;
		cmd.resp_type = MMC_RSP_R1;
		cmd.args = 0;

		rc = bcm2835_send_cmd(mmchs, &cmd, NULL);

		if(rc)
			return rc;

		cmd.cmd = SD_CMD_APP_SEND_OP_COND;
		cmd.resp_type = MMC_RSP_R3;
		cmd.args = (mmchs->voltages & 0xff8000);

		if(mmchs->version == SD_VERSION_2)
			cmd.args |= OCR_HCS;

		if(mmchs->uhs_enabled)
			cmd.args |= OCR_S18R;

		rc = bcm2835_send_cmd(mmchs, &cmd, NULL);

		if(rc)
			return rc;

		if(cmd.resp[0] & OCR_BUSY)
			break;

		if(timeout-- <= 0)
			return -1;

		micro_delay(1000);
	}

	if(mmchs->version != SD_VERSION_2)
		mmchs->version = SD_VERSION_1_0;

	card->ocr = cmd.resp[0];
	mmchs->high_capacity = ((card->ocr & OCR_HCS) == OCR_HCS);

	return 0;
}

static int sd_get_scr(struct bcm2835_sdhost *mmchs, struct sd_card_regs *card) {
	/* 8 byte aligned memory buffer */
	struct buffer {
		unsigned char buf[8];
	};
	struct buffer *buf_ptr;
	unsigned char *scr_ptr;
	struct mmc_cmd cmd;
	struct mmc_data data;
	int c, rc, timeout;

	buf_ptr = malloc(sizeof(struct buffer));

	if(!buf_ptr) {
		log_warn(&log, "Failed to get memory for scr buffer\n");
		return -1;
	}

	cmd.cmd = MMC_CMD_APP_CMD;
	cmd.resp_type = MMC_RSP_R1;
	cmd.args = card->rca << 16;

	rc = bcm2835_send_cmd(mmchs, &cmd, NULL);

	if(rc) {
		free(buf_ptr);
		return rc;
	}

	cmd.cmd = SD_CMD_APP_SEND_SCR;
	cmd.resp_type = MMC_RSP_R1;
	cmd.args = 0;
	
	timeout = 3;

retry_scr:
	data.dest = (char*) buf_ptr;
	data.blocksize = 8;
	data.blocks = 1;
	data.flags = MMC_DATA_READ;

	rc = bcm2835_send_cmd(mmchs, &cmd, &data);

	if(rc) {
		if(timeout--) {
			goto retry_scr;
		}
		free(buf_ptr);
		return rc;
	}

	scr_ptr = (unsigned char*) card->scr;

	for(c = 7; c >= 0; c--) 
		*scr_ptr++ = buf_ptr->buf[c];

	if(!card->scr[1] & SD_SCR1_4BIT)
		log_warn(&log, "4 bit access not supported\n");

	free(buf_ptr);

	return 0;
}

static int sd_switch(struct bcm2835_sdhost *mmchs, int mode, int group, unsigned char value, unsigned char *resp) {
	struct mmc_cmd cmd;
	struct mmc_data data;

	/* switch frequency */
	cmd.cmd = SD_CMD_SWITCH_FUNC;
	cmd.resp_type = MMC_RSP_R1;
	cmd.args = (mode << 31) | 0xffffff;
	cmd.args &= ~(0xf << (group * 4));
	cmd.args |= value << (group * 4);

	data.dest = (char*) resp;
	data.blocksize = 64;
	data.blocks = 1;
	data.flags = MMC_DATA_READ;

	return bcm2835_send_cmd(mmchs, &cmd, &data);
}

static int mmc_startup(struct bcm2835_sdhost *mmchs, struct sd_slot *slot) {
	struct mmc_cmd cmd;
	int rc, timeout;
	unsigned int sd3_bus_mode, mult;
	struct buffer {
		unsigned char buf[16];
	};
	struct buffer *switch_status;
	unsigned long long csize;

	/* put the card into identify mode */
	cmd.cmd = MMC_CMD_ALL_SEND_CID;
	cmd.resp_type = MMC_RSP_R2;
	cmd.args = 0;
	
	rc = bcm2835_send_cmd(mmchs, &cmd, NULL);

	if(rc)
		return rc;

	slot->card.regs.cid[0] = cmd.resp[0];
	slot->card.regs.cid[1] = cmd.resp[1];
	slot->card.regs.cid[2] = cmd.resp[2];
	slot->card.regs.cid[3] = cmd.resp[3];

	/* get the relative address */
	cmd.cmd = SD_CMD_SEND_RELATIVE_ADDR; 
	cmd.resp_type = MMC_RSP_R6;
	cmd.args = 0; /* card 0 */

	rc = bcm2835_send_cmd(mmchs, &cmd, NULL);

	if(rc)
		return rc;

	slot->card.regs.rca = (cmd.resp[0] >> 16) & 0xffff;
	cmd.args = slot->card.regs.rca << 16;

	/* get card specific data */
	cmd.cmd = MMC_CMD_SEND_CSD;
	cmd.resp_type = MMC_RSP_R2;
	cmd.args = slot->card.regs.rca << 16; 
	rc = bcm2835_send_cmd(mmchs, &cmd, NULL);

	if(rc)
		return rc;

	slot->card.regs.csd[0] = cmd.resp[0];
	slot->card.regs.csd[1] = cmd.resp[1];
	slot->card.regs.csd[2] = cmd.resp[2];
	slot->card.regs.csd[3] = cmd.resp[3];

	if(mmchs->version == MMC_VERSION_UNKNOWN) {
		unsigned int version = (cmd.resp[0] >> 26) & 0xf;

		switch(version) {
		case 0:
			mmchs->version = MMC_VERSION_1_2;
			break;
		case 1:
			mmchs->version = MMC_VERSION_1_4;
			break;
		case 2:
			mmchs->version = MMC_VERSION_2_2;
			break;
		case 3:
			mmchs->version = MMC_VERSION_3;
			break;
		case 4:
			mmchs->version = MMC_VERSION_4;
			break;
		default:
			mmchs->version = MMC_VERSION_1_2;
			break;
		}
	}

	if(IS_SD(mmchs->version))
		slot->card.blk_size = 1 << ((cmd.resp[1] >> 16) & 0xf);
	else
		slot->card.blk_size = 1 << ((cmd.resp[3] >> 22) & 0xf);

	if(mmchs->high_capacity) {
		csize = (slot->card.regs.csd[1] & 0x3f) << 16 | (slot->card.regs.csd[2] & 0xffff0000) >> 16;
		mult = 8;
	}
	else {
		csize = (slot->card.regs.csd[1] & 0x3ff) << 2 | (slot->card.regs.csd[2] & 0xc0000000) >> 30;
		mult = (slot->card.regs.csd[2] & 0x00038000) >> 15;
	}

	slot->card.blk_count = (csize + 1) << (mult + 2);

	if(slot->card.blk_size > MMC_MAX_BLOCK_LEN)
		slot->card.blk_size = MMC_MAX_BLOCK_LEN;

	slot->card.state = SD_MODE_DATA_TRANSFER_MODE;
	slot->card.part[0].dv_base = 0;
	slot->card.part[0].dv_size = (u64_t) slot->card.blk_count * slot->card.blk_size;

	mmchs->clock = 25000000;
	bcm2835_sdhost_set_clock(mmchs->clock);

	/* select the card and put it into transfer mode */
	cmd.cmd = MMC_CMD_SELECT_CARD;
	cmd.resp_type = MMC_RSP_R1;
	cmd.args = slot->card.regs.rca << 16;

	rc = bcm2835_send_cmd(mmchs, &cmd, NULL);

	if(rc)
		return rc;

	if(sd_get_scr(mmchs, &slot->card.regs)) {
		log_warn(&log, "Failed to get scr register\n");
		return -1;
	}

	/* switch to high speed */
	switch_status = malloc(sizeof(struct buffer));

	if(!switch_status) {
		log_warn(&log, "Failed to get switch status memory\n");
		return -1;
	}

	timeout = 4;
	while(timeout--) {
		rc = sd_switch(mmchs, SD_SWITCH_CHECK, 0, 1, (unsigned char*)switch_status);

		if(rc) {
			free(switch_status);
			return rc;
		}

		if(!switch_status->buf[7] & SD_HIGHSPEED_BUSY)
			mmchs->capabilities |= SD_MODE_HS;
	}

	sd3_bus_mode = switch_status->buf[3] >> 16 & 0x1f;
	if(sd3_bus_mode & SD_MODE_UHS_SDR104)
		mmchs->capabilities |= SD_MODE_UHS_SDR104;
	if(sd3_bus_mode & SD_MODE_UHS_SDR50)
		mmchs->capabilities |= SD_MODE_UHS_SDR50;
	if(sd3_bus_mode & SD_MODE_UHS_SDR25)
		mmchs->capabilities |= SD_MODE_UHS_SDR25;
	if(sd3_bus_mode & SD_MODE_UHS_SDR12)
		mmchs->capabilities |= SD_MODE_UHS_SDR12;
	if(sd3_bus_mode & SD_MODE_UHS_DDR50)
		mmchs->capabilities |= SD_MODE_UHS_DDR50;

	free(switch_status);

	return 0;
}

static int sd_set_bus_width(struct bcm2835_sdhost *mmchs, struct sd_card_regs *card) {
	struct mmc_cmd cmd; 
	int rc;

	cmd.cmd = MMC_CMD_APP_CMD;
	cmd.resp_type = MMC_RSP_R1;
	cmd.args = card->rca << 16;

	rc = bcm2835_send_cmd(mmchs, &cmd, NULL);

	if(rc)
		return rc;

	cmd.cmd = SD_CMD_APP_SET_BUS_WIDTH;
	cmd.resp_type = MMC_RSP_R1;
	cmd.args = 2; /* 4 bit mode */

	rc = bcm2835_send_cmd(mmchs, &cmd, NULL);

	if(rc)
		return rc;

	return 0;
}

static int sd_set_card_speed(struct bcm2835_sdhost *mmchs) {
	int rc;
	unsigned int mode;
	struct buffer {
		unsigned char buf[16];
	};
	struct buffer *switch_status;

	mode = mmchs->capabilities & 0xf;
	if(mode & SD_MODE_UHS_SDR104)
		mode = SD_MODE_UHS_SDR104;
	else if(mode & SD_MODE_UHS_SDR50)
		mode = SD_MODE_UHS_SDR50;
	else if(mode & SD_MODE_UHS_SDR25)
		mode = SD_MODE_UHS_SDR25;
	else if(mode & SD_MODE_UHS_SDR12)
		mode = SD_MODE_UHS_SDR12;
	else if(mode & SD_MODE_UHS_DDR50)
		mode = SD_MODE_UHS_DDR50;
	else {
		log_warn(&log, "ERROR no valid bus speed modes in capabilities.\n");
		return -ENODEV;
	}

	switch_status = malloc(sizeof(struct buffer));

	if(!switch_status) {
		log_warn(&log, "Failed to get card speed memory\n");
		return -ENODEV;
	}

	rc = sd_switch(mmchs, SD_SWITCH_SWITCH, 0, mode, (unsigned char*) switch_status);

	free(switch_status);

	return rc;
}

static int sd_read_ssr(struct bcm2835_sdhost *mmchs, struct sd_card_regs *card) {
	static const unsigned int sd_au_size[] = {
		0, SZ_16K/512, SZ_32K/512, SZ_64K/512,
		SZ_128K/512, SZ_256K/512, SZ_512K/512, SZ_1M/512,
		SZ_2M/512, SZ_4M/512, SZ_8M/512, (SZ_8M+SZ_4M)/512,
		SZ_16M/512, (SZ_16M+SZ_8M)/512, SZ_32M/512, SZ_64M/512};
	int rc, i, timeout;
	unsigned int au, eo, et, es;
	struct mmc_cmd cmd;
	struct mmc_data data;
	struct buffer {
		unsigned char data[16];
	};
	struct buffer *result;

	cmd.cmd = MMC_CMD_APP_CMD;
	cmd.resp_type = MMC_RSP_R1;
	cmd.args = card->rca << 16;

	rc = bcm2835_send_cmd(mmchs, &cmd, NULL);

	if(rc)
		return rc;

	result = malloc(sizeof(struct buffer));

	if(!result) {
		log_warn(&log, "Failed to get memory for ssr\n");
		return -1;
	}

	cmd.cmd = SD_CMD_APP_SD_STATUS;
	cmd.resp_type = MMC_RSP_R1;
	cmd.args = 0;

	data.dest = (char*) result;
	data.blocksize = 64;
	data.blocks = 1;
	data.flags = MMC_DATA_READ;

	timeout = 3;
retry_ssr:
	rc = bcm2835_send_cmd(mmchs, &cmd, &data);
	if(rc) {
		if(timeout--) {
			goto retry_ssr;
		}
		free(result);
		return rc;	
	}

	for(i = 0; i < 16; i++) 
		result->data[i] = be32_to_le(result->data[i]);

	au = (result->data[2] >> 12) & 0xf;
	if((au <= 9) || (mmchs->version == SD_VERSION_3)) {
		mmchs->alloc_unit = sd_au_size[au];
		es = (result->data[3] >> 24) & 0xff;
		es |= (result->data[2] & 0xff) << 8;
		et = (result->data[3] >> 18) & 0x3f;
		if(es && et) {
			eo = (result->data[3] >> 16) & 0x3;
			mmchs->erase_timeout = (et * 1000) / es;
			mmchs->erase_offset = eo * 1000;
		}
	}
	else {
		log_warn(&log, "Invalid allocation unit size\n");
	}

	return 0;
}

static int mmc_io_rw_direct_host(struct bcm2835_sdhost *host, int write, unsigned int fn, unsigned int addr, unsigned char in, unsigned char *out) {
	struct mmc_cmd cmd = {};
	int rc;

	if(fn > 7)
		return -EINVAL;

	if(addr & ~ 0x1ffff)
		return -EINVAL;

	cmd.cmd = SDIO_CMD_DIRECT;
	cmd.args = write ? 0x80000000: 0x00000000;
	cmd.args |= fn << 28;
	cmd.args |= (write && out) ? 0x80000000 : 00000000;
	cmd.args |= addr << 9;
	cmd.args |= in;
	cmd.resp_type = MMC_RSP_R5;

	rc = bcm2835_send_cmd(host, &cmd, NULL);
	if(rc)
		return rc;

	if(cmd.resp[0] & R5_ERROR)
		return -EIO;
	if(cmd.resp[0] & R5_FUNCTION_NUMBER)
		return -EINVAL;
	if(cmd.resp[0] & R5_OUT_OF_RANGE)
		return -ERANGE;

	if(out) 
		*out = (cmd.resp[0] >> 8) & 0xff;
	else
		*out = cmd.resp[0] &0xff;

	return 0;
}

static int mmc_send_op_cond_iter(struct bcm2835_sdhost *host, int use_arg) {
	struct mmc_cmd cmd;
	int rc;

	cmd.cmd = MMC_CMD_SEND_OP_COND;
	cmd.resp_type = MMC_RSP_R3;
	cmd.args = 0;
	if(use_arg)
		cmd.args = OCR_HCS |
			(host->voltages &
			(host->ocr & OCR_VOLTAGE_MASK)) |
			(host->ocr & OCR_ACCESS_MODE);
	rc = bcm2835_send_cmd(host, &cmd, NULL);
	if(rc)
		return rc;
	host->ocr = cmd.resp[0];
	return 0;
}

static int mmc_send_op_cond(struct bcm2835_sdhost *host) {
	int rc, i;

	mmc_go_idle(host);

	for(i = 0; i < 2; i++) {
		rc = mmc_send_op_cond_iter(host, i != 0);
		if(rc)
			return rc;

		if(host->ocr & OCR_BUSY)
			break;
	}
	return 0; 
}

struct sd_card* mmchs_card_initialize(struct sd_slot *slot) {
	struct sd_card *card;

	card = &slot->card;
	memset(card, 0, sizeof(struct sd_card));
	card->slot = slot;

	mmchs->clock = 0;
	bcm2835_sdhost_set_clock(mmchs->clock); /* disable the clock */
	bcm2835_sdhost_set_ios(mmchs);

	mmchs->clock = 122129;
	bcm2835_sdhost_set_clock(mmchs->clock); 
	bcm2835_sdhost_set_ios(mmchs);

	if(mmc_go_idle(mmchs)) {
		log_warn(&log, "Failed to go idle state\n");
		return 0;
	}

	if(mmc_send_if_cond(mmchs)) {
		log_warn(&log, "Failed test for sd v2\n");
		mmc_send_op_cond(mmchs);
		return 0;
	}

	if(sd_send_op_cond(mmchs, &slot->card.regs)) {
		log_warn(&log, "Failed to get operational condition\n");
		return 0;
	}

	if(mmc_startup(mmchs, slot)) {
		log_warn(&log, "Failed to configure card\n");
		return 0;
	}

	if(sd_set_bus_width(mmchs, &slot->card.regs)) {
		log_warn(&log, "Failed to set bus width\n");
		return 0;
	}

	if(sd_set_card_speed(mmchs)) {
		log_warn(&log, "Failed to set card speed\n");
		return 0;
	}

	if(sd_read_ssr(mmchs, &slot->card.regs)) {
		log_warn(&log, "Failed to read allocation unit size\n");
		return 0;
	}

	return card;
}

void mmchs_set_log_level(int level) {
	if(level >= 0 && level <= 4)
		log.log_level = level;
}

int mmchs_host_reset(struct mmc_host *host) {
	return 0;
}

int mmchs_card_detect(struct sd_slot *slot) {
	return 1;
}

int mmchs_card_release(struct sd_card *card) {
	assert(card->open_ct == 1);
	card->open_ct--;
	card->state = SD_MODE_UNINITIALIZED;

	sd_set_bus_width(mmchs, &card->regs); /* Set bus width 4 bits */
}

static void mmchs_hw_intr(unsigned int irqs) {
	log_warn(&log, "Hardware interrupt left over (0x%08lx)\n", mmc_read32(mmchs->io_base + mmchs->regs->SDHSTS));

	if(sys_irqenable(&hook_id) != OK)
		printf("couldn't re-enable interrupt\n");
}

static int bcm2835_read_blocks(struct bcm2835_sdhost *host, void *dest, u32_t start, u32_t blkcnt, u32_t blk_size) {
	struct mmc_cmd cmd;
	struct mmc_data data;

	if(blkcnt > 1)
		cmd.cmd = MMC_CMD_READ_MULTIPLE_BLOCK;
	else
		cmd.cmd = MMC_CMD_READ_SINGLE_BLOCK;

	if(host->high_capacity)
		cmd.args = start;
	else
		cmd.args = start * blk_size;

	cmd.resp_type = MMC_RSP_R1;

	data.dest = dest;
	data.blocks = blkcnt;
	data.blocksize = blk_size;
	data.flags = MMC_DATA_READ;

	if(bcm2835_send_cmd(host, &cmd, &data))
		return 0;

	if(blkcnt > 1) {
		cmd.cmd = MMC_CMD_STOP_TRANSMISSION;
		cmd.args = 0;
		cmd.resp_type = MMC_RSP_R1b;
		if(bcm2835_send_cmd(host, &cmd, NULL)) {
			log_warn(&log, "mmc failed to send stop cmd\n");
			return 0;
		}
	}
	
	return blkcnt;
}

static int bcm2835_write_blocks(struct sd_card *card, struct bcm2835_sdhost *host, u32_t start, u32_t blkcnt, const void *src) {
	struct mmc_cmd cmd;
	struct mmc_data data;
	int timeout = 1000;

	if((start + blkcnt) > card->blk_count) {
		log_warn(&log, "MMC: block number 0x%08x exceeds max 0x%08x\n", start + blkcnt, card->blk_count);
		return 0;
	}

	if(blkcnt == 0)
		return 0;
	else
		if(blkcnt == 1)
			cmd.cmd = MMC_CMD_WRITE_SINGLE_BLOCK;
		else
			cmd.cmd = MMC_CMD_WRITE_MULTIPLE_BLOCK;

	if(host->high_capacity)
		cmd.args = start;
	else
		cmd.args = start * card->blk_size;

	cmd.resp_type = MMC_RSP_R1;

	data.src = src;
	data.blocks = blkcnt;
	data.blocksize = card->blk_size;
	data.flags = MMC_DATA_WRITE;

	if(bcm2835_send_cmd(host, &cmd, &data)) {
		log_warn(&log, "MMC write failed\n");
		return 0;
	}

	if(blkcnt > 1) {
		cmd.cmd = MMC_CMD_STOP_TRANSMISSION;
		cmd.args = 0;
		cmd.resp_type = MMC_RSP_R1b;
		if(bcm2835_send_cmd(host, &cmd, NULL)) {
			log_warn(&log, "MMC cmd stop failed\n");
			return 0;
		}
	}
}

static int mmchs_host_read(struct sd_card *card, u32_t block_number, u32_t count, unsigned char *buf) {
	u32_t i;

	i = count;
	for(i=0;i<count;i++) {
		bcm2835_read_blocks(mmchs, buf, block_number + i, count, card->blk_size);
		buf += (i * card->blk_size);
	}
	return OK;
}

static int mmchs_host_write(struct sd_card *card, u32_t block_number, u32_t count, unsigned char *buf) {
	u32_t i;

	for(i=0;i<count;i++) {
		bcm2835_write_blocks(card, mmchs, block_number + i, count, buf + (i * card->blk_size));
	}
	return OK;
}

static int get_clock_rate(u32_t clock_id) {
	int mbox_fd, rc;
	u32_t buf[8];

	mbox_fd = open("/dev/mailbox", O_RDWR);

	if(mbox_fd < 0) {
		log_warn(&log, "ERROR opening /dev/mailbox\n");
		return mbox_fd;
	}

	buf[0] = 32;	/* total size */
	buf[1] = 0;		/* request */
	buf[2] = 0x00030002;	/* tag */
	buf[3] = 8;		/* buffer size */
	buf[4] = 4;		/* request size */
	buf[5] = clock_id;		/* device id */
	buf[6] = 0;		/* state */	
	buf[7] = 0;		/* end tag */
	rc = write(mbox_fd, buf, 32);
	rc = read(mbox_fd, buf, 32);	

	close(mbox_fd);
	return buf[6];
}

void host_initialize_host_structure_mmchs(struct mmc_host *host) {
	int i;
	mmchs = &bcm2835_mmchs;

	host->host_set_instance = mmchs_host_set_instance;
	host->host_init = mmchs_host_init;
	host->set_log_level = mmchs_set_log_level;
	host->host_reset = mmchs_host_reset;
	host->card_detect = mmchs_card_detect;
	host->card_initialize = mmchs_card_initialize;
	host->card_release = mmchs_card_release;
	host->hw_intr = mmchs_hw_intr;
	host->read = mmchs_host_read;
	host->write = mmchs_host_write;
	
	for(i = 0; i < sizeof(host->slot) / sizeof(host->slot[0]); i++) {
		host->slot[i].host = host;
		host->slot[i].card.slot = &host->slot[i];
	}
}
