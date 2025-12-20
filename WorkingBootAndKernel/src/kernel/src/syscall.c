#include <stdint.h>
#include "scheduler.h"
#include "syscall_numbers.h"

uint64_t syscall_dispatch(uint64_t num)
{
    uart_puts("[kernel] syscall invoked!\n");
    
    switch (num) {
        case SYSCALL_YIELD:
            scheduler_yield();
            return 0;
        default:
            return (uint64_t)-1;
    }
}
