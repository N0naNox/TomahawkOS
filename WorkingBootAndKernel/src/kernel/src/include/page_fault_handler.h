#ifndef PAGE_FAULT_HANDLER_H
#define PAGE_FAULT_HANDLER_H

#include <stdint.h>
#include "idt.h"

//Page fault causes definitions
#define PF_CAUSE_PRESENT      0x1  //page-protection violation, 0 = non-present page
#define PF_CAUSE_WRITE        0x2  // fault caused by a write
#define INVALID_ADDRESS  0x3  //fault occurred because of different address


/* Handle a page fault (called from assembly) */
int page_fault_handler(uint64_t error_code, uint64_t faulting_address, regs_t *regs);

int page_not_present_handler(uint64_t faulting_address);
int page_protection_violation_handler(uint64_t faulting_address);


#endif 
