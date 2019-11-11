#ifndef __BSP_COMPAT_H_
#define __BSP_COMPAT_H__

char *pi_compat_tb[] = {
	"raspberrypi,2-model-b",
	"brcm,bcm2709",
	"brcm,bcm2710",
	"brcm,bcm2836",
	"raspberrypi,3-model-b",
	"brcm,bcm2837",
	NULL
};

char **compatible_table[] = {
	pi_compat_tb,
	NULL
};

#endif
