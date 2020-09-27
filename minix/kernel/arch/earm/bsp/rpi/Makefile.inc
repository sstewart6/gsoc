#
# BSP for RPI hardware
#

.PATH: ${.CURDIR}/arch/${MACHINE_ARCH}/bsp/rpi

SRCS+=	rpi_init.c rpi_serial.c rpi_timer.c rpi_padconf.c rpi_intr.c \
	rpi_reset.c rpi_sd.c rpi_v7timer.c rpi_uart0_init.c

