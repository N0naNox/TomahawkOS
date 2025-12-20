#include "syscall.h"

void user_main(void) {
    while (1) {
        sys_yield();
    }
}
