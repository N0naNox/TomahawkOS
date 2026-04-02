/**
 * @file boot.h
 * @author ajxs
 * @date Aug 2019
 * @brief Boot functionality.
 * Contains definitions for boot structures.
 */

#ifndef BOOT_H
#define BOOT_H

#include <stdint.h>

/**
 * @brief Memory region descriptor.
 * Describes a region of memory. This is passed to the kernel by the bootloader.
 */
typedef struct s_memory_region_desc {
	uint32_t type;
	uintptr_t physical_start;
	uintptr_t virtual_start;
	uint64_t count;
	uint64_t attributes;
} Memory_Map_Descriptor;

typedef struct s_boot_video_info {
	void* framebuffer_pointer;
	uint32_t horizontal_resolution;
	uint32_t vertical_resolution;
	uint32_t pixels_per_scaline;
	uint64_t framebuffer_size;   /* total bytes: width * height * 4, rounded up */
} Kernel_Boot_Video_Mode_Info;

/**
 * @brief Boot info struct.
 * Contains information passed to the kernel at boot time by the bootloader.
 */
typedef struct s_boot_info {
	Memory_Map_Descriptor* memory_map;
	uint64_t memory_map_size;
	uint64_t memory_map_descriptor_size;
	Kernel_Boot_Video_Mode_Info video_mode_info;
	/** Physical address of the loaded initrd.img cpio archive (0 if absent). */
	uintptr_t initrd_base;
	/** Size in bytes of the loaded initrd (0 if absent). */
	uint64_t initrd_size;
} Boot_Info;

#endif

