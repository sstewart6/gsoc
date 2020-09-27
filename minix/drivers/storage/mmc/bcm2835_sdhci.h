#ifndef _BCM2835_SDHCI_H_
#define _BCM2835_SDHCI_H_

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

#define BLK_VEN_SIZE	40
#define BLK_PRD_SIZE	20
#define BLK_REV_SIZE	8

#define MILLA_TO_MICRO	1000

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

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

u32_t bcm2835_get_part(void);
void bcm2835_set_part(u32_t);
char *bcm2835_get_vendor(void);
char *bcm2835_get_product(void);
char *bcm2835_get_revision(void);
int power_on_sd_card();

#define dsb() __asm__ __volatile__ ("dsb sy\n" : : : "memory")
#define mb() __asm__ __volatile__ ("dmb 3\n" : : : "memory")
#define rmb() __asm__ __volatile__ ("dmb 1\n" : : : "memory")
#define wmb() __asm__ __volatile__ ("dmb 2\n" : : : "memory")

static inline void mmc_write8(const volatile u32_t addr, u8_t b) {
	__asm__ __volatile__("strb %0, %1\n"
		:
		: "r" (b), "m" (*(volatile u8_t *)addr)
		: "memory");
}

static inline void mmc_write16(const volatile u32_t addr, u16_t h) {
	__asm__ __volatile__("strh %0, %1\n"
		:
		: "r" (h), "m" (*(volatile u8_t *)addr)
		: "memory");
}

static inline void mmc_write32(const volatile u32_t addr, u32_t w) {
	wmb();
	__asm__ __volatile__("str %0, %1\n"
		:
		: "r" (w), "m" (*(volatile u32_t *)addr)
		: "memory");
}

static inline u8_t mmc_read8(const volatile u32_t addr) {
	u8_t b;
	__asm__ __volatile__("ldrb %0, %1\n"
		: "=r" (b)
		: "m" (*(volatile u8_t *)addr)
		: "memory");
	return b;
}

static inline u16_t mmc_read16(const volatile u32_t addr) {
	u16_t w;
	__asm__ __volatile__("ldrh %0, %1\n"
		: "=r" (w)
		: "m" (*(volatile u8_t *)addr)
		: "memory");
	return w;
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

#endif /* _BCM2835_SDHCI_H_ */
