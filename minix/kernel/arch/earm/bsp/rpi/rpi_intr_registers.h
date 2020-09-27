#ifndef _RPI_INTR_H
#define _RPI_INTR_H

#define RPI2_INTR_BASE          0x3f00B000
#define RPI4_INTR_BASE          0xfe00b000
/* #define RPI4_INTR_BASE          0x40041000 */

#define RPI2_INTR_BASIC_PENDING 0x200
#define RPI2_INTR_PENDING1      0x204
#define RPI2_INTR_PENDING2      0x208
#define RPI2_INTR_FIQ_CTRL      0x20c
#define RPI2_INTR_ENABLE1       0x210
#define RPI2_INTR_ENABLE2       0x214
#define RPI2_INTR_ENABLE_BASIC  0x218
#define RPI2_INTR_DISABLE1      0x21c
#define RPI2_INTR_DISABLE2      0x220
#define RPI2_INTR_DISABLE_BASIC 0x224

/* ARMC interrupt mapping on GIC */
#define ARMC_GIC_TIMER			64
#define ARMC_GIC_MBOX			65
#define ARMC_GIC_DOORBELL0		66
#define ARMC_GIC_DOORBELL1		67
#define ARMC_GIC_VPU0HALTED		68
#define ARMC_GIC_VPU1HALTED		69
#define ARMC_GIC_ARM_ADDR_ERR	70
#define ARMC_GIC_ARM_AXI_ERR	71
#define ARMC_GIC_SWI0			72
#define ARMC_GIC_SWI1			73
#define ARMC_GIC_SWI2			74
#define ARMC_GIC_SWI3			75
#define ARMC_GIC_SWI4			76
#define ARMC_GIC_SWI5			77
#define ARMC_GIC_SWI6			78
#define ARMC_GIC_SWI7			79

#define RPI4_MBOX_INT   65

#endif /* _RPI_INTR_H */
