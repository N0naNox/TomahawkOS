#pragma once

#include <stdint.h>

static inline void wrmsr(uint32_t msr, uint64_t val);
void syscall_init(void);
