#ifndef _BCM2835_SDHOST_H_
#define _BCM2835_SDHOST_H_

#define MMC_LEGACY 25000000
#define SD_LEGACY 25000000
#define MMC_HS 26000000
#define SD_HS 50000000
#define MMC_HS_52 52000000
#define MMC_DDR_52 52000000
#define UHS_SDR12 25000000
#define UHS_SDR25 50000000
#define UHS_SDR50 100000000
#define UHS_DDR50 50000000
#define UHS_SDR104 208000000
#define MMC_HS_200 200000000

#define FIFO_READ_THRESHOLD 4
#define FIFO_WRITE_THRESHOLD 4
#define SDEDM_WRITE_THRESHOLD_SHIFT 9
#define SDEDM_READ_THRESHOLD_SHIFT 14
#define SDEDM_THRESHOLD_MASK 0x1f
#define SDEDM_FSM_MASK 0xf
#define SDEDM_FSM_IDENTMODE 0x0
#define SDEDM_FSM_DATAMODE 0x1
#define SDEDM_FSM_READDATA 0x2
#define SDEDM_FSM_WRITEDATA 0x3
#define SDEDM_FSM_READWAIT 0x4
#define SDEDM_FSM_READCRC 0x5
#define SDEDM_FSM_WRITECRC 0x6
#define SDEDM_FSM_WRITEWAIT1 0x7
#define SDEDM_FSM_POWERDOWN 0x8
#define SDEDM_FSM_POWERUP 0x9
#define SDEDM_FSM_WRITESTART1 0xa
#define SDEDM_FSM_WRITESTART2 0xb
#define SDEDM_FSM_GENPULSES 0xc
#define SDEDM_FSM_WRITEWAIT2 0xd
#define SDEDM_FSM_STARTPOWDOWN 0xf
#define SDEDM_FORCE_DATA_MODE (1<<19)

#define SDDATA_FIFO_WORDS 16

#define SDHCFG_BUSY_IRPT_EN (1<<10)
#define SDHCFG_BLOCK_IRPT_EN (1<<8)
#define SDHCFG_SDIO_IRPT_EN (1<<5)
#define SDHCFG_DATA_IRPT_EN (1<<4)
#define SDHCFG_SLOW_CARD (1<<3)
#define SDHCFG_WIDE_EXT_BUS (1<<2)
#define SDHCFG_WIDE_INT_BUS (1<<1)
#define SDHCFG_REL_CMD_LINE (1<<0)
#define SDCDIV_MAX_CDIV 0x7ff
#define SANE_TIMEOUT 500000
#define SDCMD_NEW_FLAG 0x8000
#define SDCMD_FAIL_FLAG 0x4000
#define SDCMD_BUSYWAIT 0x800
#define SDCMD_NO_RESPONSE 0x400
#define SDCMD_LONG_RESPONSE 0x200
#define SDCMD_WRITE_CMD 0x80
#define SDCMD_READ_CMD 0x40
#define SDCMD_CMD_MASK 0x3f
#define SDHSTS_BUSY_IRPT 0x400
#define SDHSTS_BLOCK_IRPT 0x200
#define SDHSTS_SDIO_IRPT 0x100
#define SDHSTS_REW_TIME_OUT 0x80
#define SDHSTS_CMD_TIME_OUT 0x40
#define SDHSTS_CRC16_ERROR 0x20
#define SDHSTS_CRC7_ERROR 0x10
#define SDHSTS_FIFO_ERROR 0x08
#define SDHSTS_CLEAR_MASK	(SDHSTS_BUSY_IRPT | \
	SDHSTS_BLOCK_IRPT | \
	SDHSTS_SDIO_IRPT | \
	SDHSTS_REW_TIME_OUT | \
	SDHSTS_CMD_TIME_OUT | \
	SDHSTS_CRC16_ERROR | \
	SDHSTS_CRC7_ERROR | \
	SDHSTS_FIFO_ERROR)
#define SDHSTS_TRANSFER_ERROR_MASK (SDHSTS_CRC7_ERROR|SDHSTS_CRC16_ERROR|SDHSTS_REW_TIME_OUT|SDHSTS_FIFO_ERROR)
#define SDHSTS_ERROR_MASK (SDHSTS_CMD_TIME_OUT|SDHSTS_TRANSFER_ERROR_MASK)
#define SDHST_TIMEOUT_MAX_USEC 100000

#define MMC_RSP_PRESENT (1 << 0)
#define MMC_RSP_136 (1 << 1) /* 136 bit response */
#define MMC_RSP_CRC (1 << 2) /* expect valid crc */
#define MMC_RSP_BUSY (1 << 3) /* card may send busy */
#define MMC_RSP_OPCODE (1 << 4) /* response contains opcode */

#define MMC_CMD_GO_IDLE_STATE 0
#define MMC_CMD_SEND_OP_COND  1
#define MMC_CMD_ALL_SEND_CID  2
#define MMC_CMD_SET_RELATIVE_ADDR 3
#define MMC_CMD_SET_DSR 4
#define MMC_CMD_SWITCH 6
#define MMC_CMD_SELECT_CARD 7
#define MMC_CMD_SEND_EXT_CSD 8
#define MMC_CMD_SEND_CSD 9
#define MMC_CMD_SEND_CID 10
#define MMC_CMD_STOP_TRANSMISSION 12
#define MMC_CMD_SEND_STATUS 13
#define MMC_CMD_SET_BLOCKLEN 16
#define MMC_CMD_READ_SINGLE_BLOCK 17
#define MMC_CMD_READ_MULTIPLE_BLOCK 18
#define MMC_CMD_SEND_TUNING_BLOCK 19
#define MMC_CMD_SEND_TUNING_BLOCK_HS200 21
#define MMC_CMD_SET_BLOCK_COUNT 23
#define MMC_CMD_WRITE_SINGLE_BLOCK 24
#define MMC_CMD_WRITE_MULTIPLE_BLOCK 25
#define MMC_CMD_ERASE_GROUP_START 35
#define MMC_CMD_ERASE_GROUP_END 36
#define MMC_CMD_ERASE 38
#define MMC_CMD_APP_CMD 55
#define MMC_CMD_SPI_READ_OCR 58
#define MMC_CMD_SPI_CRC_ON_OFF 59
#define MMC_CMD_RES_MAN 62

#define MMC_DATA_READ 1
#define MMC_DATA_WRITE 2

#define MMC_STATUS_MASK (~0x0206BF7F)
#define MMC_STATUS_SWITCH_ERROR (1 << 7)
#define MMC_STATUS_RDY_FOR_DATA (1 << 8)
#define MMC_STATUS_CURR_STATE (0xf << 9)
#define MMC_STATUS_ERROR (1 << 19)
#define MMC_STATE_PRG (7 << 9)

#define SD_CMD_SEND_RELATIVE_ADDR 3
#define SD_CMD_SWITCH_FUNC 6
#define SD_CMD_SEND_IF_COND 8
#define SD_CMD_SWITCH_UHS18V 11

#define SD_CMD_APP_SET_BUS_WIDTH 6
#define SD_CMD_APP_SD_STATUS 13
#define SD_CMD_ERASE_WR_BLK_START 32
#define SD_CMD_ERASE_WR_BLK_END 33
#define SD_CMD_APP_SEND_OP_COND 41
#define SD_CMD_APP_SEND_SCR 51
#define SD_SWITCH_CHECK 0
#define SD_SWITCH_SWITCH 1
#define SDIO_CMD_DIRECT 52
#define SDIO_CCCR_ABORT 0x06
#define UHS_SDR12_BUS_SPEED 0
#define HIGH_SPEED_BUS_SPEED 1
#define UHS_SDR25_BUS_SPEED 1
#define UHS_SDR50_BUS_SPEED 2
#define UHS_SDR104_BUS_SPEED 3

#define MMC_RSP_PRESENT (1 << 0)
#define MMC_RSP_136 (1 << 1) /* 136 bit response */
#define MMC_RSP_CRC (1 << 2) /* expect valid crc */
#define MMC_RSP_BUSY (1 << 3) /* card may send busy */
#define MMC_RSP_OPCODE (1 << 4) /* response contains opcode */

#define MMC_RSP_NONE (0)
#define MMC_RSP_R1 (MMC_RSP_PRESENT|MMC_RSP_CRC|MMC_RSP_OPCODE)
#define MMC_RSP_R1b (MMC_RSP_PRESENT|MMC_RSP_CRC|MMC_RSP_OPCODE|MMC_RSP_BUSY)
#define MMC_RSP_R2 (MMC_RSP_PRESENT|MMC_RSP_136|MMC_RSP_CRC)
#define MMC_RSP_R3 (MMC_RSP_PRESENT)
#define MMC_RSP_R4 (MMC_RSP_PRESENT)
#define MMC_RSP_R5 (MMC_RSP_PRESENT|MMC_RSP_CRC|MMC_RSP_OPCODE)
#define MMC_RSP_R6 (MMC_RSP_PRESENT|MMC_RSP_CRC|MMC_RSP_OPCODE)
#define MMC_RSP_R7 (MMC_RSP_PRESENT|MMC_RSP_CRC|MMC_RSP_OPCODE)

#define MMC_VDD_165_195     0x00000080  /* VDD voltage 1.65 - 1.95 */
#define MMC_VDD_20_21       0x00000100  /* VDD voltage 2.0 ~ 2.1 */
#define MMC_VDD_21_22       0x00000200  /* VDD voltage 2.1 ~ 2.2 */
#define MMC_VDD_22_23       0x00000400  /* VDD voltage 2.2 ~ 2.3 */
#define MMC_VDD_23_24       0x00000800  /* VDD voltage 2.3 ~ 2.4 */
#define MMC_VDD_24_25       0x00001000  /* VDD voltage 2.4 ~ 2.5 */
#define MMC_VDD_25_26       0x00002000  /* VDD voltage 2.5 ~ 2.6 */
#define MMC_VDD_26_27       0x00004000  /* VDD voltage 2.6 ~ 2.7 */
#define MMC_VDD_27_28       0x00008000  /* VDD voltage 2.7 ~ 2.8 */
#define MMC_VDD_28_29       0x00010000  /* VDD voltage 2.8 ~ 2.9 */
#define MMC_VDD_29_30       0x00020000  /* VDD voltage 2.9 ~ 3.0 */
#define MMC_VDD_30_31       0x00040000  /* VDD voltage 3.0 ~ 3.1 */
#define MMC_VDD_31_32       0x00080000  /* VDD voltage 3.1 ~ 3.2 */
#define MMC_VDD_32_33       0x00100000  /* VDD voltage 3.2 ~ 3.3 */
#define MMC_VDD_33_34       0x00200000  /* VDD voltage 3.3 ~ 3.4 */
#define MMC_VDD_34_35       0x00400000  /* VDD voltage 3.4 ~ 3.5 */
#define MMC_VDD_35_36       0x00800000  /* VDD voltage 3.5 ~ 3.6 */

#define MMC_MODE_LEGACY (1<<0)
#define MMC_MODE_HS ((1<<2) | (1<<3))
#define MMC_MODE_HS_52MHz (1<<4)
#define MMC_MODE_DDR_52MHz (<<5)
#define MMC_MODE_HS200 (1<<11)

#define MMC_MODE_8BIT (1<<30)
#define MMC_MODE_4BIT (1<<29)
#define MMC_MODE_1BIT (1<<28)
#define MMC_MODE_SPI (1<<27)

#define SD_VERSION_SD (1<<31)
#define MMC_VERSION_MMC (1<<30)

#define IS_SD(x) ((x) & SD_VERSION_SD)
#define IS_MMC(x) ((x) & MMC_VERSION_MMC)

#define SD_VERSION_3 SD_VERSION_SD | (3<<16)
#define SD_VERSION_2 SD_VERSION_SD | (2<<16)
#define SD_VERSION_1_0 SD_VERSION_SD | (1<<16)
#define SD_VERSION_1_10 SD_VERSION_SD | (1<<16 | 10<<8)
#define SD_SCR1_4BIT (1<<2)
#define SD_MODE_HS (1<<3)
#define SD_HIGHSPEED_BUSY 0x00020000
#define SD_HIGHSPEED_SUPPORTED 0x00020000
#define SD_MODE_UHS_SDR12 (1<<0)
#define SD_MODE_UHS_SDR25 (1<<1)
#define SD_MODE_UHS_SDR50 (1<<2)
#define SD_MODE_UHS_SDR104 (1<<3)
#define SD_MODE_UHS_DDR50 (1<<4)

#define SD_MODE_UNINITIALIZED 0
#define SD_MODE_CARD_IDENTIFICATION 1
#define SD_MODE_DATA_TRANSFER_MODE 2

#define MMC_VERSION_UNKNOWN 0
#define MMC_VERSION_1_2 ((1<<30) | (1<<16) | (2<<8) | 0)
#define MMC_VERSION_1_4 ((1<<30) | (1<<16) | (4<<8) | 0)
#define MMC_VERSION_2_2 ((1<<30) | (2<<16) | (2<<8) | 0)
#define MMC_VERSION_3 ((1<<30) | (3<<16) | (0<<8) | 0)
#define MMC_VERSION_4 ((1<<30) | (4<<16) | (0<<8) | 0)
#define MMC_VERSION_4_1 ((1<<30) | (4<<16) | (1<<8) | 0)
#define MMC_VERSION_4_2 ((1<<30) | (4<<16) | (2<<8) | 0)
#define MMC_VERSION_4_3 ((1<<30) | (4<<16) | (3<<8) | 0)
#define MMC_VERSION_4_4 ((1<<30) | (4<<16) | (4<<8) | 0)
#define MMC_VERSION_4_41 ((1<<30) | (4<<16) | (4<<8) | 1)
#define MMC_VERSION_4_5 ((1<<30) | (4<<16) | (5<<8) | 0)
#define MMC_VERSION_5_0 ((1<<30) | (5<<16) | (0<<8) | 0)
#define MMC_VERSION_5_1 ((1<<30) | (5<<16) | (1<<8) | 0)

#define MMC_MAX_BLOCK_LEN 512
/*
  SDIO status in R5
  Type
    e : error bit
    s : status bit
    r : detected and set for the actual command response
    x : detected and set during command execution. the host must poll
            the card by sending status command in order to read these bits.
  Clear condition
    a : according to the card state
    b : always related to the previous command. Reception of
            a valid command will clear it (with a delay of one command)
    c : clear by read
 */

#define R5_COM_CRC_ERROR    (1 << 15)   /* er, b */
#define R5_ILLEGAL_COMMAND  (1 << 14)   /* er, b */
#define R5_ERROR        (1 << 11)   /* erx, c */
#define R5_FUNCTION_NUMBER  (1 << 9)    /* er, c */
#define R5_OUT_OF_RANGE     (1 << 8)    /* er, c */
#define R5_STATUS(x)        (x & 0xCB00)
#define R5_IO_CURRENT_STATE(x)  ((x & 0x3000) >> 12) /* s, b */

#define OCR_BUSY 0x80000000
#define OCR_HCS 0x40000000
#define OCR_S18R 0x1000000
#define OCR_VOLTAGE_MASK 0x007FFF80
#define OCR_ACCESS_MODE 0x60000000

#define SZ_1K 0x00000400
#define SZ_2K 0x00000800
#define SZ_4K 0x00001000
#define SZ_8K 0x00002000
#define SZ_16K 0x00004000
#define SZ_32K 0x00008000
#define SZ_64K 0x00010000
#define SZ_128K 0x00020000
#define SZ_256K 0x00040000
#define SZ_512K 0x00080000

#define SZ_1M 0x00100000
#define SZ_2M 0x00200000
#define SZ_4M 0x00400000
#define SZ_8M 0x00800000
#define SZ_16M 0x01000000
#define SZ_32M 0x02000000
#define SZ_64M 0x04000000

#define SDDATA_FIFO_PIO_BURST 8

#define SDEDM_FIFO_FILL_SHIFT 4
#define SDEDM_FIFO_FILL_MASK 0x1f

#define MBOX_GET_POWER 0x00020001
#define MBOX_GET_CLOCK 0x00030002
#define MBOX_SET_POWER 0x00028001

#endif /* _BCM2835_SDHOST_H_ */
