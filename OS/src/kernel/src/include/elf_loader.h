#ifndef ELF_LOADER_H
#define ELF_LOADER_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>

int elf_load_executable(const void* elf_data, uintptr_t pml4_phys, uintptr_t* entry_out);

#endif