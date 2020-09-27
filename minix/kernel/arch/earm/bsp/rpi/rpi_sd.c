#include <sys/types.h>
#include <io.h>
#include "rpi_gpio.h"

/*
Set GPIO pins for SD controller
GPIO48 SD0_CLK
GPIO49 SD0_CMD
GPIO50 SD0_DAT0
GPIO51 SD0_DAT1
GPIO52 SD0_DAT2
GPIO53 SD0_DAT3
*/
void rpi_sd_init(void) {

	/* clear bits 26-24 */
	mmio_clear(GPIO_BASE + GPIO_GPFSEL4, (7 << 24)); 
	/* set bits 26-24 b100 func 0 fsel48 SD0_CLK */
	mmio_set(GPIO_BASE + GPIO_GPFSEL4, (4 << 24)); 

	/* clear bits 30-27 */
	mmio_clear(GPIO_BASE + GPIO_GPFSEL4, (0xf << 27)); 
	/* set bits 30-27 b100 func 0 fsel49 SD0_CMD */
	mmio_set(GPIO_BASE + GPIO_GPFSEL4, (4 << 27)); 

	mmio_set(GPIO_BASE + GPIO_GPPUD, 0);
	mmio_set(GPIO_BASE + GPIO_GPPUD, ((4 << 24) | (4 << 27)));
	mmio_set(GPIO_BASE + GPIO_GPPUDCLK0, ((4 << 24) | (4 << 27)));

	int i = 150;
	while(i)
		--i;

	/* clear bits 0-2 */
	mmio_clear(GPIO_BASE + GPIO_GPFSEL5, 7); 
	/* set bits 0-2 b100 func 0 fsel50 SD0_DAT0 */
	mmio_set(GPIO_BASE + GPIO_GPFSEL5, 4); 

	/* clear bits 5-3 */
	mmio_clear(GPIO_BASE + GPIO_GPFSEL5, (7 << 3)); 
	/* set bits 0-2 b100 func 0 fsel51 SD0_DAT1 */
	mmio_set(GPIO_BASE + GPIO_GPFSEL5, (4 << 3)); 

	/* clear bits 8-6 */
	mmio_clear(GPIO_BASE + GPIO_GPFSEL5, (7 << 6)); 
	/* set bits 0-2 b100 func 0 fsel52 SD0_DAT2 */
	mmio_set(GPIO_BASE + GPIO_GPFSEL5, (4 << 6)); 

	/* clear bits 11-9 */
	mmio_clear(GPIO_BASE + GPIO_GPFSEL5, (7 << 9)); 
	/* set bits 0-2 b100 func 0 fsel53 SD0_DAT3 */
	mmio_set(GPIO_BASE + GPIO_GPFSEL5, (4 << 9)); 

	mmio_set(GPIO_BASE + GPIO_GPPUD, 0);
	mmio_set(GPIO_BASE + GPIO_GPPUDCLK1, ((4 | (4 << 3) | (4 << 6) | (4 << 9))));

	i = 150;
	while(i)
		--i;
}
