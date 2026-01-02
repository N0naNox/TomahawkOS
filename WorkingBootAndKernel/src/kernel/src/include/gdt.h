#pragma once
#include <stdint.h>

void gdt_init(void);

/* Segment selectors */
#define KERNEL_CS 0x08
#define KERNEL_DS 0x10
#define USER_DS   0x23
#define USER_CS   0x1B
