#include <sys/types.h>
#include "bsp_init.h"
#include "bsp_padconf.h"
#include "bsp_reset.h"
#include "rpi_sd.h"

void
rpi_init()
{
	/* map memory for padconf */
	rpi_padconf_init();

	/* map memory for reset control */
	rpi_reset_init();

	/* disable watchdog */
	rpi_disable_watchdog();

	/* init sd host controller */
	rpi_sd_init();
}
