# Makefile for the mmc driver.
PROGS=      mmc emmc
MAN.mmc=
MAN.emmc=

.if ${BSP_NAME} == "rpi"
.if ${RPI_VER} == 4
SRCS.mmc = bcm2835_sdhci.c mmcblk.c mmc.c mmc_write.c sdhci.c mmchost_dummy.c bcm2835_sdhci.h mmc.h sdhci.h mbox.h mmc_types.h 
.else
CPPFLAGS.bcm2835_sdhost.c+= -I${NETBSDSRCDIR}/minix
CPPFLAGS.bcm2835_sdhost.c+= -I${NETBSDSRCDIR}/minix/kernel/arch/earm/include
SRCS.mmc = bcm2835_sdhost.c mmcblk.c mmchost_dummy.c bcm2835_sdhost.h sdhcreg.h
.endif
.else
SRCS.mmc = mmchost_mmchs.c mmcblk.c mmchost_dummy.c sdhcreg.h sdmmcreg.h
.endif
SRCS.emmc= emmc.c mmcblk.c

DPADD+=	${LIBBLOCKDRIVER} ${LIBSYS}
LDADD+=	-lgcc_eh -lblockdriver -lsys
CLEANFILES+=.depend mmcblk.d

#
# This is a system driver.
CPPFLAGS+= -D_SYSTEM=1

.include <minix.service.mk>
