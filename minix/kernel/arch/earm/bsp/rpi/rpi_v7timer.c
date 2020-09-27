#include <sys/types.h>
#include <kernel/kernel.h>
#include <kernel/proc.h>
#include <kernel/proto.h>

#include <minix/type.h>
#include <io.h>
#include "rpi_v7timer.h"

/*	Write virtual countdown timer value (CNTV_TVAL) */
void write_virt_countdown_timer(register u32_t timer_value) {
	asm volatile("mcr p15, 0, %0, c14, c3, 0" : : "r"(timer_value));
}

/*	Write physical countdown timer value (CNTP_TVAL) */
void write_phys_countdown_timer(register u32_t timer_value) {
	asm volatile("mcr p15, 0, %0, c14, c2, 0" : : "r"(timer_value));
}

/*	Read virtual countdown timer value (CNTV_TVAL) */
u32_t read_virt_countdown_timer() {
	register u32_t timer_value;

	asm volatile("mrc p15, 0, %0, c14, c3, 0" : "=r"(timer_value) :);
	return timer_value;
}

/*	Read physical countdown timer value (CNTP_TVAL) */
u32_t read_phys_countdown_timer() {
	register u32_t timer_value;

	asm volatile("mrc p15, 0, %0, c14, c2, 0" : "=r"(timer_value) :);
	return timer_value;
}

/*	Write virtual timer control reg (CNTV_CTL) */
void enable_virt_timer() {
	register u32_t value = TIMER_ASSERTED | TIMER_ENABLED;
	asm volatile("mcr p15, 0, %0, c14, c3, 1" : : "r"(value));
}

/*	Write physical timer control reg (CNTP_CTL) */
void enable_phys_timer() {
	register u32_t value = TIMER_ASSERTED | TIMER_ENABLED;
	asm volatile("mcr p15, 0, %0, c14, c2, 1" : : "r"(value));
}

/*	Read virtual timer control reg (CNTV_CTL) */
u32_t read_virt_timer_ctl() {
	register u32_t timer_ctl;

	asm volatile("mrc p15, 0, %0, c14, c3, 1" : "=r"(timer_ctl) :);
	return timer_ctl;
}

/*	Read physical timer control reg (CNTP_CTL) */
u32_t read_phys_timer_ctl() {
	register u32_t timer_ctl;

	asm volatile("mrc p15, 0, %0, c14, c2, 1" : "=r"(timer_ctl) :);
	return timer_ctl;
}

u32_t get_clock_frequency() {
	register u32_t value;
	
	asm volatile("mrc p15, 0, %0, c14, c0, 0" : "=r"(value) :);
	return value;
}
