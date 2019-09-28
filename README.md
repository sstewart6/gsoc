# gsoc
Minix3 raspberry pi 2 and 3 sd card device driver

This fork adds a SD card device driver to the port of Minix3 to Raspberry pi.  It has been configured to run on a physical RPI2/3.  The releasetools/arm_sdimage_rpi.sh script will compile minix and create an SD card image file using the standard RPI firmware and minix filesystems.  This should be enough to boot minix on a RPI. The standard Broadcom bootcode.bin and start.elf will be called to start Broadcom portion of the chip and will then use the config.txt to call the kernel.  The kernel will load minix into memory and then branch to it.

There are no graphics or USB drivers yet.  To run you will need to connect to the console over a serial line with something like a FTDI FT232RL chip and software like minicom.

minicom configuration parameters:
Bps/Par/Bits : 115200 8N1
Hardware Flow Control : No

On the RPI3 the main UART has been replaced with Bluetooth.

https://www.briandorey.com/post/raspberry-pi-3-uart-boot-overlay-part-two

In order to re-enable the UART, a device tree overlay has been added.  The config.txt created by the build will enable this overlay.

There is an issue with builing gmake.  Newer version of make must have this fixed by now, but the version that is called in by minix does not.

in external/gpl2/gmake/dist/configure the line:

if _GNU_GLOB_INTERFACE_VERSION == GLOB_INTERFACE_VERSION

needs to be changed to :

if _GNU_GLOB_INTERFACE_VERSION >= GLOB_INTERFACE_VERSION

To build run
cd minix
export MAKECONF=etc/mk.conf
./releasetools/arm_sdimage_rpi.sh
