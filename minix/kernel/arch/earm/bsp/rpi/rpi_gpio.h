#ifndef _RPI_GPIO_H
#define _RPI_GPIO_H

#define GPIO_BASE 0x3f200000
#define GPIO_GPFSEL0 0x00
#define GPIO_GPFSEL1 0x04
#define GPIO_GPFSEL2 0x08
#define GPIO_GPFSEL3 0x0c
#define GPIO_GPFSEL4 0x10
#define GPIO_GPFSEL5 0x14
#define GPIO_GPPUD 0x94
#define GPIO_GPPUDCLK0 0x98
#define GPIO_GPPUDCLK1 0x9c
#define GPIO_FSEL14_F0 (4<<12)
#define GPIO_FSEL15_F0 (4<<15)
#define GPIO_FSEL32_F3 (7<<6)
#define GPIO_FSEL33_F3 (7<<9)

#endif /* _RPI_GPIO_H */
