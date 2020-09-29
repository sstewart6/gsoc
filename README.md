# gsoc
Minix3 raspberry pi 2/3 and 4 sd card device driver

This fork adds a SD card device driver to the port of Minix3 to Raspberry pi.  It has been configured to run on a physical RPI2/3 or 4.
The releasetools/arm_sdimage_rpi.sh script will compile minix and create an SD card image file using the standard RPI firmware and 
minix filesystems.  This should be enough to boot minix on a RPI. The standard Broadcom bootcode.bin and start.elf will be called 
to start Broadcom portion of the chip and will then use the config.txt to call the kernel.  The kernel will load minix into memory 
and then branch to it. <br />

There are no graphics or USB drivers yet.  To run you will need to connect to the console over a serial line with something like a 
FTDI FT232RL chip and software like minicom. <br />

minicom configuration parameters: <br />
Bps/Par/Bits : 115200 8N1 <br />
Hardware Flow Control : No <br />

On the RPI3 the main UART has been replaced with Bluetooth. <br />

https://www.briandorey.com/post/raspberry-pi-3-uart-boot-overlay-part-two

In order to re-enable the UART, a device tree overlay has been added.  The config.txt created by the build will enable this overlay. <br />

There is an issue with builing gmake.  Newer version of make must have this fixed by now, but the version that is called in by minix does not. <br />

in external/gpl2/gmake/dist/configure the line: <br />
if _GNU_GLOB_INTERFACE_VERSION == GLOB_INTERFACE_VERSION <br />
needs to be changed to : <br />
if _GNU_GLOB_INTERFACE_VERSION >= GLOB_INTERFACE_VERSION <br />

## Build for RPI2/3

To build run <br />
cd minix <br />
export MAKECONF=etc/mk.conf <br />
export HOST_CFLAGS="-O2 -fcommon" <br />
./releasetools/arm_sdimage_rpi.sh <br />

## Build for RPI4
cd minix <br />
export MAKECONF=etc/mk.conf <br />
export HOST_CFLAGS="-O2 -fcommon" <br />
export RPI_VER=4" <br />
./releasetools/arm_sdimage_rpi.sh <br />

gcc v10 defaults to -fno-common which causes errors for global variables in the tools build, so the -fcommon option is required.
