#include <sys/types.h>
#include <machine/cpu.h>
#include <minix/type.h>
#include <minix/board.h>
#include <io.h>

#include "kernel/kernel.h"
#include "kernel/proc.h"
#include "kernel/vm.h"
#include "kernel/proto.h"
#include "arch_proto.h"
#include "hw_intr.h"

#include "rpi_intr_registers.h"
#include "rpi_timer_registers.h"
#include "rpi_v7timer.h"

static struct rpi2_intr
{
	vir_bytes base;
	vir_bytes core_base;
	int size;
} rpi2_intr;

static kern_phys_map intr_phys_map;
static kern_phys_map timer_phys_map;

static irq_hook_t dummy8_irq_hook;
static irq_hook_t dummy40_irq_hook;
static irq_hook_t dummy41_irq_hook;
static irq_hook_t dummy51_irq_hook;

int
dummy_irq_handler()
{
	/*
	 * The Raspberry Pi has a bunch of cascaded interrupts that are useless
	 * for MINIX. This handler catches them so as not to pollute the console
	 * with spurious interrupts messages.
	 */
	return 0;
}

/*  Set priority level for the interrupt (GICD_IPRIORITY)
	curr_prio - current priority */
static void set_int_priority(u32_t interrupt, u32_t priority) {
	u32_t wrd_offset, bit_offset, curr_prio;

	wrd_offset = interrupt ? (interrupt / 4) * 4 : 0;
	bit_offset = interrupt ? (interrupt % 4) * 8 : 0;
	priority &= 0xff;
	priority = priority << bit_offset;
	curr_prio = mmio_read(rpi2_intr.base + DISTBASE_OFFSET + GICD_IPRIORITY + wrd_offset);
	curr_prio |= priority;
	mmio_write(rpi2_intr.base + DISTBASE_OFFSET + GICD_IPRIORITY + wrd_offset, curr_prio);
}

/*  Configure Interrupt Processor Targets Register (GICD_ITARGETSR)
    cpu_no - cpu number
    wrd_offset - word offset
    char_offset - character offset
    cpu_mask - cpu mask bits */
static void set_int_proc_tgt(u32_t int_id, u32_t cpu_no) {
    u32_t wrd_offset, char_offset;
    u32_t cpu_mask = 0;

    wrd_offset = int_id ? (int_id / 4) * 4 : 0;
    char_offset = int_id ? (int_id % 4) : 0;

    cpu_mask = mmio_read(rpi2_intr.base + DISTBASE_OFFSET + GICD_ITARGETSR + wrd_offset);
    cpu_mask |= (1 << cpu_no) << (char_offset * 8);
    mmio_write(rpi2_intr.base + DISTBASE_OFFSET + GICD_ITARGETSR + wrd_offset, cpu_mask);
}

int
rpi_intr_init(const int auto_eoi)
{
	if (BOARD_IS_RPI_2_B(machine.board_id) ||
	    BOARD_IS_RPI_3_B(machine.board_id)) {
		rpi2_intr.base = RPI2_INTR_BASE;
		rpi2_intr.core_base = RPI2_QA7_BASE;
	} else if(BOARD_IS_RPI_4_B(machine.board_id)) {
		/* Read Configuration Base Address Register (CBAR) */
		register vir_bytes cbar;
		asm volatile("mrc p15, 1, %0, c15, c3, 0" : "=r"(cbar));
		rpi2_intr.base = cbar;
		rpi2_intr.core_base = rpi2_intr.base + DISTBASE_OFFSET;
	} else {
		panic
		    ("Can not do the interrupt setup. machine (0x%08x) is unknown\n",
		    machine.board_id);
	}

	if(BOARD_IS_RPI_4_B(machine.board_id)) {
		u32_t int_ctl_type;
		u32_t no_int_lines;

		rpi2_intr.size = 0x10000;	/* 64K */
		kern_phys_map_ptr(rpi2_intr.base, rpi2_intr.size,
	   		VMMF_UNCACHED | VMMF_WRITE,
	   	 	&intr_phys_map, (vir_bytes) & rpi2_intr.base);

		int_ctl_type = mmio_read(rpi2_intr.base + DISTBASE_OFFSET + GICD_TYPER);
		no_int_lines = int_ctl_type & 0x0f;
		(no_int_lines *= 32) + 1;

		if(RPI4_TIMER_INT > no_int_lines)
		    panic ("Error max interrupts/requested interrupt %08x/%08x\n", no_int_lines, RPI4_TIMER_INT);

		/* Disable interrupts GICD_CTLR */
		mmio_write(rpi2_intr.base + DISTBASE_OFFSET, 0x0);

		enable_phys_timer();

		/* Enable distributor control register (GICD_CTLR) to send interrupts */
		mmio_write(rpi2_intr.base + DISTBASE_OFFSET + GICD_CTLR, 0x1);

		/* Enable interrupt signals to CPU interface (GICC_CTLR) */	
		mmio_write(rpi2_intr.base + CPUBASE_OFFSET + GICC_CTLR, 0x1);

		write_phys_countdown_timer(get_clock_frequency() / RPI4_COUNTDOWN_TIMER);
	}
	else {
		rpi2_intr.size = 0x1000;	/* 4K */

		kern_phys_map_ptr(rpi2_intr.base, rpi2_intr.size,
	   		VMMF_UNCACHED | VMMF_WRITE,
	   	 	&intr_phys_map, (vir_bytes) & rpi2_intr.base);
		kern_phys_map_ptr(rpi2_intr.core_base, rpi2_intr.size,
	   		VMMF_UNCACHED | VMMF_WRITE,
	   		&timer_phys_map, (vir_bytes) & rpi2_intr.core_base);

		/* Disable FIQ and all interrupts */
		mmio_write(rpi2_intr.base + RPI2_INTR_FIQ_CTRL, 0);
		mmio_write(rpi2_intr.base + RPI2_INTR_DISABLE_BASIC, 0xFFFFFFFF);
		mmio_write(rpi2_intr.base + RPI2_INTR_DISABLE1, 0xFFFFFFFF);
		mmio_write(rpi2_intr.base + RPI2_INTR_DISABLE2, 0xFFFFFFFF);

		/* Enable ARM timer routing to IRQ here */
		mmio_write(rpi2_intr.core_base + QA7_CORE0TIMER, 0x8);

		/* Register dummy irq handlers */
		put_irq_handler(&dummy8_irq_hook, 8, dummy_irq_handler);
		put_irq_handler(&dummy40_irq_hook, 40, dummy_irq_handler);
		put_irq_handler(&dummy41_irq_hook, 41, dummy_irq_handler);
		put_irq_handler(&dummy51_irq_hook, 51, dummy_irq_handler);
	}

	return 0;
}

void
rpi_irq_handle(void)
{
	if(BOARD_IS_RPI_4_B(machine.board_id)) {
		/* Read interrupt and acknowledge (GICC_IAR) */
		u32_t intr_id = mmio_read(rpi2_intr.base + CPUBASE_OFFSET + GICC_IAR); 
		intr_id &= 0x01ff;
		irq_handle(intr_id);

		/* Write End of Interrupt (GICC_EOIR) */
		mmio_write(rpi2_intr.base + CPUBASE_OFFSET + GICC_EOIR, intr_id);
	}
	else {
		/* Function called from assembly to handle interrupts */
		uint32_t irq_0_31 = mmio_read(rpi2_intr.core_base + QA7_CORE0INT);
		uint32_t irq_32_63 = mmio_read(rpi2_intr.base + RPI2_INTR_BASIC_PENDING);
		uint32_t irq_64_95 = mmio_read(rpi2_intr.base + RPI2_INTR_PENDING1);
		uint64_t irq_96_128 = mmio_read(rpi2_intr.base + RPI2_INTR_PENDING2);

		int irq = 0;

		/* Scan all interrupts bits */
		for (irq = 0; irq < 128; irq++) {
			int is_pending = 0;
			if (irq < 32)
				is_pending = irq_0_31 & (1 << irq);
			else if (irq < 64)
				is_pending = irq_32_63 & (1 << (irq-32));
			else if (irq < 96)
				is_pending = irq_64_95 & (1 << (irq-64));
			else
				is_pending = irq_96_128 & (1 << (irq-96));

			if (is_pending)
				irq_handle(irq);
		}

		/* Clear all pending interrupts */
		mmio_write(rpi2_intr.base + RPI2_INTR_BASIC_PENDING, irq_32_63);
		mmio_write(rpi2_intr.base + RPI2_INTR_PENDING1, irq_64_95);
		mmio_write(rpi2_intr.base + RPI2_INTR_PENDING2, irq_96_128);
	}
}

void
rpi_irq_unmask(int irq)
{
	if(BOARD_IS_RPI_4_B(machine.board_id)) {
		set_int_priority(irq, 0x80);
		set_int_proc_tgt(irq, 0);

		/* Enable the interrupt in the Interrupt Set-enable Register (GICD_ISENABLER) */
		u32_t reg_offs, bit_no, intr_map;

		reg_offs = irq ? (irq / 32) * 4 : 0;
		bit_no = irq ? irq % 32 : 0;

		intr_map = mmio_read(rpi2_intr.base + DISTBASE_OFFSET + GICD_ISENABLER + reg_offs);
		intr_map |= (1 << bit_no);
		mmio_write(rpi2_intr.base + DISTBASE_OFFSET + GICD_ISENABLER + reg_offs, intr_map);
	}
	else if (irq < 32)
		/* Nothing to do */
		;
	else if (irq < 64)
		mmio_write(rpi2_intr.base + RPI2_INTR_ENABLE_BASIC, 1 << (irq-32));
	else if (irq < 96)
		mmio_write(rpi2_intr.base + RPI2_INTR_ENABLE1, 1 << (irq-64));
	else if (irq < 128)
		mmio_write(rpi2_intr.base + RPI2_INTR_ENABLE2, 1 << (irq-96));
}

void
rpi_irq_mask(const int irq)
{
	if(BOARD_IS_RPI_4_B(machine.board_id)) {
		/* Disable the interrupt in the Interrupt Clear-enable Register (GICD_ICENABLER) */
		u32_t reg_offs, bit_no;

		reg_offs = irq ? (irq / 32) * 4 : 0;
		bit_no = irq ? irq % 32 : 0;

		mmio_write(rpi2_intr.base + DISTBASE_OFFSET + GICD_ICENABLER + reg_offs, (1 << bit_no));
	}
	else if (irq < 32)
		/* Nothing to do */
		;
	else if (irq < 64)
		mmio_write(rpi2_intr.base + RPI2_INTR_DISABLE_BASIC, 1 << (irq-32));
	else if (irq < 96)
		mmio_write(rpi2_intr.base + RPI2_INTR_DISABLE1, 1 << (irq-64));
	else if (irq < 128)
		mmio_write(rpi2_intr.base + RPI2_INTR_DISABLE2, 1 << (irq-96));
}
