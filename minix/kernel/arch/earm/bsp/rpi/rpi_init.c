#include <sys/types.h>
#include "bsp_init.h"
#include "bsp_padconf.h"
#include "bsp_reset.h"
#include "rpi_gpio.h"
#include <minix/type.h>
#include <minix/board.h>

void rpi_sd_init(void);
void rpi_uart0_init(void);
extern struct machine machine;

void
rpi_init()
{
	/* map memory for padconf */
	rpi_padconf_init();

	/* map memory for reset control */
	rpi_reset_init();

	/* disable watchdog */
	rpi_disable_watchdog();

	if(!BOARD_IS_RPI_4_B(machine.board_id)) {
		/* enable sd card */
		rpi_sd_init();
		rpi_uart0_init();
	}
}
