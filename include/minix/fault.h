#define _MINIX

#define FAULT_INJECTOR_CMD_OFF  0
#define FAULT_INJECTOR_CMD_ON   1
#define FAULT_INJECTOR_CMD_TEST 2
#define FAULT_INJECTOR_CMD_PRINT_STATS 3

void fault_switch(int enable);

void fault_test();

#ifdef _MINIX
#include <minix/com.h>
#include <minix/ipc.h>

int do_fault_injector_request(message *m);
int do_fault_injector_request_impl(message *m);
#endif

