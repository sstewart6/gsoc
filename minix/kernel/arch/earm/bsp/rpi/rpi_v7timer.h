#ifndef __RPI_V7TIMER_H__
#define __RPI_V7TIMER_H__
#define RPI4_TIMER_INT	30
#define TIMER_ASSERTED	0x4
#define TIMER_MASKED	0x2
#define TIMER_ENABLED	0x1
#define DISTBASE_OFFSET	0x1000	/* GIC distributor offset from CBAR */
#define CPUBASE_OFFSET	0x2000	/* GIC CPU interface offset from CBAR */
#define GICC_PMR		0x4		/* GIC CPU Priority Mask Reg */
#define GICC_CTLR		0x0		/* GIC CPU interface control register */
#define GICC_IAR		0xc		/* GIC CPU interrupt acknowledge reg */
#define GICC_EOIR		0x10	/* GIC CPU End of Interrupt */
#define GICD_CTLR		0x0		/* GIC distributor control register */
#define GICD_TYPER		0x4		/* GIC distributor Interrupt controller type register */
#define GICD_ISENABLER	0x100	/* GIC distributor set-enable reg */
#define GICD_ICENABLER	0x180	/* GIC distributor clear-enable reg */
#define GICD_ITARGETSR	0x800	/* GIC distributor interrupt processor targets reg */
#define GICD_ICFGR		0xc00	/* GIC distributor interrupt configuration register */
#define GICD_IPRIORITY	0x400	/* GIC distributor interrupt priority register */
#define RPI4_COUNTDOWN_TIMER	1000

vir_bytes read_cbar();
void write_virt_countdown_timer(register u32_t timer_value);
void write_phys_countdown_timer(register u32_t timer_value);
u32_t read_virt_countdown_timer();
u32_t read_phys_countdown_timer();
void enable_virt_timer();
void enable_phys_timer();
u32_t read_virt_timer_ctl();
u32_t read_phys_timer_ctl();
u32_t get_clock_frequency();
#endif /*  __RPI_V7TIMER_H__ */
