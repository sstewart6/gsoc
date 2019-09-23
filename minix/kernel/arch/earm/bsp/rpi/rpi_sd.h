#ifndef _RPI_SD_H
#define _RPI_SD_H

#define GPIO_BASE 0x3f200000
#define GPIO_GPFSEL0 0x00
#define GPIO_GPFSEL1 0x04
#define GPIO_GPFSEL2 0x08
#define GPIO_GPFSEL3 0x0c
#define GPIO_GPFSEL4 0x10
#define GPIO_GPFSEL5 0x14

void rpi_sd_init(void);

#endif /* _RPI_SD_H */
