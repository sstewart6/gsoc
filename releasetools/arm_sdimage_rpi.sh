#!/usr/bin/env bash
set -e

#
# This script creates a bootable image and should at some point in the future
# be replaced by the proper NetBSD infrastructure.
#

#
# Source settings if present
#
: ${SETTINGS_MINIX=.settings}
if [ -f "${SETTINGS_MINIX}"  ]
then
	echo "Sourcing settings from ${SETTINGS_MINIX}"
	# Display the content (so we can check in the build logs
	# what the settings contain.
	cat ${SETTINGS_MINIX} | sed "s,^,CONTENT ,g"
	. ${SETTINGS_MINIX}
fi

BSP_NAME=rpi
: ${ARCH=evbearm-el}
: ${TOOLCHAIN_TRIPLET=arm-elf32-minix-}
: ${BUILDSH=build.sh}

: ${SETS="minix-base minix-comp minix-games minix-man minix-tests tests"}
: ${IMG=minix_arm_sd.img}

# ARM definitions:
: ${BUILDVARS=-V MKGCCCMDS=yes -V MKLLVM=no -N2}
# These BUILDVARS are for building with LLVM:
#: ${BUILDVARS=-V MKLIBCXX=no -V MKKYUA=no -V MKATF=no -V MKLLVMCMDS=no}
: ${FAT_SIZE=$((    500*(2**20) / 512))} # This is in sectors

case $(uname -s) in
Darwin)
	MKFS_VFAT_CMD=newfs_msdos
	MKFS_VFAT_OPTS="-h 64 -u 32 -S 512 -s ${FAT_SIZE} -o 0"
;;
FreeBSD)
	MKFS_VFAT_CMD=newfs_msdos
	MKFS_VFAT_OPTS=
;;
*)
	MKFS_VFAT_CMD=mkfs.vfat
	MKFS_VFAT_OPTS=
;;
esac

for needed in mcopy dd ${MKFS_VFAT_CMD} git
do
	if ! which $needed 2>&1 > /dev/null
	then
		echo "**Skipping image creation: missing tool '$needed'"
		exit 1
	fi
done

# we create a disk image of about 3 gig's
# for alignment reasons, prefer sizes which are multiples of 4096 bytes
: ${IMG_SIZE=$((     3*(2**30) ))}
: ${ROOT_SIZE=$((  256*(2**20) ))}
: ${HOME_SIZE=$((  128*(2**20) ))}
: ${USR_SIZE=$((  2648*(2**20) ))}

# set up disk creation environment
. releasetools/image.defaults
. releasetools/image.functions

# all sizes are written in 512 byte blocks
ROOTSIZEARG="-b $((${ROOT_SIZE} / 512 / 8))"
USRSIZEARG="-b $((${USR_SIZE} / 512 / 8))"
HOMESIZEARG="-b $((${HOME_SIZE} / 512 / 8))"

#
# Get the rpi firmware.
#
if [ ! -e releasetools/rpi-firmware ]
then
	wget ${RPI_FIRMWARE_URL}
	unzip -q master.zip firmware-master/boot/* -d releasetools/rpi-firmware
	rm master.zip
fi

# where the kernel & boot modules will be
MODDIR=${DESTDIR}/boot/minix/.temp

echo "Building work directory..."
build_workdir "$SETS"

echo "Adding extra files..."

# create a fstab entry in /etc
cat >${ROOT_DIR}/etc/fstab <<END_FSTAB
/dev/c0d0p2	/usr		mfs	rw			0	2
/dev/c0d0p3	/home		mfs	rw			0	2
none		/sys		devman	rw,rslabel=devman	0	0
none		/dev/pts	ptyfs	rw,rslabel=ptyfs	0	0
END_FSTAB
add_file_spec "etc/fstab" extra.fstab

echo "Creating specification files..."
create_input_spec
create_protos "usr home"

#
# Create the FAT partition, which contains the bootloader files, kernel and modules
#
dd if=/dev/zero of=${WORK_DIR}/fat.img bs=512 count=1 seek=$(($FAT_SIZE -1)) 2>/dev/null

#
# Format the fat partition.
#
${MKFS_VFAT_CMD} ${MKFS_VFAT_OPTS} ${WORK_DIR}/fat.img

#
# Make the bootloader. 
#
make -f releasetools/rpi-bootloader/Makefile.gnu MOD_DIR=${MODDIR} RELEASE_DIR=${RELEASETOOLSDIR}

#
# Copy the kernel package, rpi firmware and config files to fat img.
#
mcopy -bsp -i ${WORK_DIR}/fat.img  ${MODDIR}/minix_rpi.bin ::minix_rpi.bin
mcopy -bsp -i ${WORK_DIR}/fat.img  ${RELEASETOOLSDIR}/rpi-firmware/firmware-master/boot/* ::
mcopy -bsp -i ${WORK_DIR}/fat.img  ${RELEASETOOLSDIR}/rpi-bootloader/cmdline.txt ::
mcopy -bsp -i ${WORK_DIR}/fat.img  ${RELEASETOOLSDIR}/rpi-bootloader/cmdline3.txt ::
mcopy -bsp -i ${WORK_DIR}/fat.img  ${RELEASETOOLSDIR}/rpi-bootloader/config.txt ::

# Clean image
if [ -f ${IMG} ]	# IMG might be a block device
then
	rm -f ${IMG}
fi

#
# Create the empty image where we later will put the partitions in.
#
dd if=/dev/zero of=${IMG} bs=512 count=1 seek=$((($IMG_SIZE / 512) -1))

#
# Generate /root, /usr and /home partition images.
#
echo "Writing disk image..."
FAT_START=2048 # those are sectors
ROOT_START=$(($FAT_START + $FAT_SIZE))
echo " * ROOT"
_ROOT_SIZE=$(${CROSS_TOOLS}/nbmkfs.mfs -vvv -d ${ROOTSIZEARG} -I $((${ROOT_START}*512)) ${IMG} ${WORK_DIR}/proto.root)
_ROOT_SIZE=$(($_ROOT_SIZE / 512))
USR_START=$((${ROOT_START} + ${_ROOT_SIZE}))
echo " * USR"
_USR_SIZE=$(${CROSS_TOOLS}/nbmkfs.mfs  -d ${USRSIZEARG}  -I $((${USR_START}*512))  ${IMG} ${WORK_DIR}/proto.usr)
_USR_SIZE=$(($_USR_SIZE / 512))
HOME_START=$((${USR_START} + ${_USR_SIZE}))
echo " * HOME"
_HOME_SIZE=$(${CROSS_TOOLS}/nbmkfs.mfs -d ${HOMESIZEARG} -I $((${HOME_START}*512)) ${IMG} ${WORK_DIR}/proto.home)
_HOME_SIZE=$(($_HOME_SIZE / 512))

#
# Write the partition table using the natively compiled
# minix partition utility
#
${CROSS_TOOLS}/nbpartition -f -m ${IMG} ${FAT_START} "c:${FAT_SIZE}*" 81:${_ROOT_SIZE} 81:${_USR_SIZE} 81:${_HOME_SIZE}

#
# Merge the partitions into a single image.
#
echo "Merging file systems"
dd if=${WORK_DIR}/fat.img of=${IMG} seek=$FAT_START conv=notrunc

echo "Disk image at `pwd`/${IMG}"
