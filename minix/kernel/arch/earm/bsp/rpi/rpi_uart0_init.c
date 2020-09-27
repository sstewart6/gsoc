#include <sys/types.h>
#include <io.h>
#include "rpi_gpio.h"

void rpi_uart0_init(void) {
	u32_t gpio_reg, i;

/* If uart0 has been set to function 3 on pins 32 & 33 (see config.txt
 * disable-bt) then clear it so we can use it on pins 15 & 15. */
	gpio_reg = mmio_read(GPIO_BASE + GPIO_GPFSEL3);
	gpio_reg &= (GPIO_FSEL32_F3 | GPIO_FSEL33_F3);
	if(gpio_reg == (GPIO_FSEL32_F3 | GPIO_FSEL33_F3)) {
		mmio_clear(GPIO_BASE + GPIO_GPFSEL3, (GPIO_FSEL32_F3 | GPIO_FSEL33_F3));
		mmio_write(GPIO_BASE + GPIO_GPPUD, 0);
		i = 150;
		while(i)
			i--;
		mmio_set(GPIO_BASE + GPIO_GPPUDCLK1, (3 << 2));
		i = 150;
		while(i)
			i--;
	}

	mmio_set(GPIO_BASE + GPIO_GPFSEL1, (GPIO_FSEL14_F0 | GPIO_FSEL15_F0));
	mmio_set(GPIO_BASE + GPIO_GPPUD, 0);
	i = 150;
	while(i)
		i--;
	mmio_set(GPIO_BASE + GPIO_GPPUDCLK0, (3<<14));
	i = 150;
	while(i)
		i--;
}
