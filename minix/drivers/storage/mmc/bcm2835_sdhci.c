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
#include <minix/sysutil.h>
#include "sdhci.h"
#include "mbox.h"
#include "mmc.h"
#include "bcm2835_sdhci.h"

#define RPI4_SDHCI_BASE 0xfe340000
#define RPI2_SDHCI_BASE 0x3f300000
#define RPI4_SDHCI_IRQ 158
#define RPI2_SDHCI_IRQ 86 /* mmc0 80, mmc1 86 */
#define MIN_FREQ 400000
#define MMC_UHS_SUPPORT 1
#define MMC_WRITE 1
#define CONFIG_MMC_QUIRKS 1

#define likley(x) __builtin_expect((x),1)
#define unlikely(x) __builtin_expect((x),0)

#define WARN_ON(condition) ({			\
	int __ret_warn_on = !!(condition);	\
	if (unlikely(__ret_warn_on))		\
		printf("WARNING at %s:%d/%s()!\n", __FILE__, __LINE__, __func__); \
	unlikely(__ret_warn_on);	\
})

int micro_delay(u32_t);
#define udelay(val) micro_delay(val)

static struct log log = {
	.name = "bcm2835_sdhci.c",
	.log_level = LEVEL_INFO,
	.log_func = default_log
};

static int hook_id = 0;

struct bcm2835_sd_card {
	struct sd_card sd;
	u32_t hwpart;
	char vendor[BLK_VEN_SIZE + 1];
	char product[BLK_PRD_SIZE + 1];
	char revision[BLK_REV_SIZE + 1];
};

struct bcm2835_sdhci_host {
	vir_bytes io_size;
	vir_bytes hw_base;
	int irq_nr;
	struct sdhci_host host;
	struct bcm2835_sd_card *bcm2835_card;
	struct mmc mmc;
	struct mmc_cmd *cmd;
	struct mmc_data *data;
	struct mmc_ops *ops;
	u32_t twoticks_delay;
	u32_t last_write;
};

struct bcm2835_sd_card bcm2835_mmc;

struct mmc_config bcm2835_mmc_cfg;

struct bcm2835_sdhci_host bcm2835_host = {
	.io_size = 0x100,
	.hw_base = 0xfe340000,
	.irq_nr = 158,
	.bcm2835_card = &bcm2835_mmc,
};

struct bcm2835_sdhci_host *bcm_host = &bcm2835_host;

u32_t bcm2835_get_part(void) {
	return bcm2835_mmc.hwpart;
}

void bcm2835_set_part(u32_t part) {
	bcm2835_mmc.hwpart = part;
}

char *bcm2835_get_vendor(void) {
	return bcm2835_mmc.vendor;
}

char *bcm2835_get_product(void) {
	return bcm2835_mmc.product;
}

char *bcm2835_get_revision(void) {
	return bcm2835_mmc.revision;
}

int mmchs_host_set_instance(struct mmc_host *host, int instance) {

	log_info(&log, "Using instance number %d\n", instance);
	if(instance != 0)
		return EIO;
	
	return OK;
}

int power_on_sd_card() {
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
	
	bcm_host->host.clock = buf[6];

	rc = 1;

exit:
	close(mbox_fd);
	return rc;
}

void mmchs_set_log_level(int level) {
	if(level >= 0 && level <= 4)
		log.log_level = level;
}

void sdhci_reset(struct sdhci_host *host, u8 mask);

int mmchs_host_reset(struct mmc_host *host) {
	sdhci_reset(&bcm_host->host, 0x01);	/* reset all */
	return 0;
}

int mmchs_card_detect(struct sd_slot *slot) {
	return 1;
}

int mmc_deinit(struct mmc *mmc);

int mmchs_card_release(struct sd_card *card) {
	assert(card->open_ct == 1);
	card->open_ct--;
	card->state = SD_MODE_UNINITIALIZED;

	mmc_deinit(bcm_host->host.mmc);	
}

static void mmchs_hw_intr(unsigned int irqs) {
	log_warn(&log, "Hardware interrupt left over (0x%08lx)\n", mmc_read32((u32_t) bcm_host->host.ioaddr + SDHCI_INT_STATUS));

	if(sys_irqenable(&hook_id) != OK)
		printf("couldn't re-enable interrupt\n");
}

u32_t mmc_bread(struct mmc *mmc, u32_t start, u32_t blkcnt, void *dst);

static int mmchs_host_read(struct sd_card *card, u32_t block_number, u32_t count, unsigned char *buf) {
	u32_t i;

	i = count;
	for(i=0;i<count;i++) {
		mmc_bread(bcm_host->host.mmc, block_number, count, buf);
		buf += (i * card->blk_size);
	}
	return OK;
}

u32_t mmc_bwrite(struct mmc *mmc, u32_t start, u32_t blkcnt, const void *src);

static int mmchs_host_write(struct sd_card *card, u32_t block_number, u32_t count, unsigned char *buf) {
	u32_t i;

	for(i=0;i<count;i++) {
		mmc_bwrite(bcm_host->host.mmc, block_number + i, count, buf + (i * card->blk_size));
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

struct sd_card* mmchs_card_initialize(struct sd_slot *slot) {
	struct sd_card *card;
	int rc;

	card = &slot->card;
	memset(card, 0, sizeof(struct sd_card));
	card->slot = slot;

	rc = mmc_init(bcm_host->host.mmc);
	if(rc) {
		return NULL;
	}
	else {
		slot->card.blk_size = bcm_host->mmc.read_bl_len;
		slot->card.blk_count = bcm_host->mmc.capacity_user / bcm_host->mmc.read_bl_len;
		slot->card.state = SD_MODE_DATA_TRANSFER_MODE;
		slot->card.part[0].dv_base = 0;
		slot->card.part[0].dv_size = bcm_host->mmc.capacity_user;
	}

	return card;
}

int sdhci_init(struct mmc *mmc);

int sdhci_setup_cfg(struct mmc_config *cfg, struct sdhci_host *host, u32_t f_max, u32_t f_min);

int mmchs_host_init(struct mmc_host *host) {
	struct minix_mem_range mem_range;
	int rc;
	message msg;
	struct machine machine;

	bcm2835_host.mmc.cfg = &bcm2835_mmc_cfg;
	bcm2835_host.mmc.priv = &bcm2835_host.host;
	bcm2835_host.host.mmc = &bcm2835_host.mmc;

	sys_getinfo(GET_MACHINE, &machine, sizeof(machine), 0, 0);
	if(BOARD_IS_RPI_4_B(machine.board_id)) {
		bcm_host->hw_base = RPI4_SDHCI_BASE;
		bcm_host->irq_nr = RPI4_SDHCI_IRQ;
	}
	else if(BOARD_IS_RPI_3_B(machine.board_id) || 
		BOARD_IS_RPI_2_B(machine.board_id)) {
		bcm_host->hw_base = RPI2_SDHCI_BASE;
		bcm_host->irq_nr = RPI2_SDHCI_IRQ;
	}
	else {
		log_warn(&log, "sdhci: unknown board type\n");
	}

	if(!power_on_sd_card()) {
		log_warn(&log, "Unable to power on SD card.\n");
		return 1;
	}
	/* Grant access to i/o memory */
	mem_range.mr_base = bcm_host->hw_base;
	mem_range.mr_limit = bcm_host->hw_base + bcm_host->io_size;
	if(sys_privctl(SELF, SYS_PRIV_ADD_MEM, &mem_range) != 0)
		panic("Unable to request permission to map memory.");

	/* Save the virtual address to i/o mem */
	bcm_host->host.ioaddr = vm_map_phys(SELF, (void*) bcm_host->hw_base, bcm_host->io_size);
	
	if(bcm_host->host.ioaddr == MAP_FAILED)
		panic("unable to map MMC memory");

	bcm_host->host.host_caps = MMC_MODE_4BIT | MMC_MODE_HS | MMC_MODE_HS_52MHz;
	bcm_host->host.voltages = MMC_VDD_32_33 | MMC_VDD_33_34;

	bcm_host->twoticks_delay = ((2 * 1000000) / MIN_FREQ) +1;
	bcm_host->last_write = 0;
	bcm_host->host.quirks = SDHCI_QUIRK_BROKEN_VOLTAGE | 
		SDHCI_QUIRK_BROKEN_R1B |
		SDHCI_QUIRK_WAIT_SEND_CMD |
		SDHCI_QUIRK_NO_HISPD_BIT;
	bcm_host->host.max_clk = bcm_host->host.clock;
	bcm_host->host.ops = NULL;

	rc = sdhci_setup_cfg(&bcm_host->host.cfg, &bcm_host->host, bcm_host->host.clock, MIN_FREQ);
	if(rc) {
		log_warn(&log, "MMC: Failed to setup SDHCI %d\n", rc);
		return rc;
	}

	rc = sys_irqsetpolicy(bcm_host->irq_nr, 0, &hook_id);
	if(rc != OK) {
		log_warn(&log, "MMC: could not set IRQ policy rc %d irq %d\n", rc, bcm_host->irq_nr);
		return 1;
	}
	else
		bcm_host->mmc.cfg = &bcm_host->host.cfg;

	memset(&msg, 0, sizeof(msg));

	rc = sys_irqsetpolicy(bcm_host->irq_nr, IRQ_DISABLE, &hook_id);

	if(rc) {
		log_warn(&log, "MMC: could not disable interrupts\n");
		return 1;
	}
	
	return sdhci_init(&bcm2835_host.mmc); 
}

void host_initialize_host_structure_mmchs(struct mmc_host *host) {
	int i;

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
