#include "syscall.h"

void _start(void) {
    while (1) {
        sys_yield();   // test syscall
    }
}
