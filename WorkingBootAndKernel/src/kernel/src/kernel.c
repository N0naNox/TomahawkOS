/**
 * @file kernel.c
 * @author ajxs
 * @date Aug 2019
 * @brief Kernel entry.
 * Contains the kernel entry point.
 */

#include <stdint.h>
#include <stddef.h>
#include "include/vga.h"
#include "include/font_8x16.h"
#include "include/idt.h"
#include "include/keyboard.h"
#include "include/frame_alloc.h"
#include "include/paging.h"
#include "include/page_fault_handler.h"
#include "include/string.h"
#include <boot.h>
#include <uart.h>
#include "timer.h"
#include "include/scheduler.h"
#include "include/proc.h"

/** Whether to draw a test pattern to video output. */
#define DRAW_TEST_SCREEN 1

#define KERNEL_VIRT_BASE 0xFFFFFFFF80000000ULL

#define TEST_SCREEN_COL_NUM             4
#define TEST_SCREEN_ROW_NUM             3
#define TEST_SCREEN_TOTAL_TILES         TEST_SCREEN_COL_NUM * TEST_SCREEN_ROW_NUM
#define TEST_SCREEN_PRIMARY_COLOUR      0x00FF40FF
#define TEST_SCREEN_SECONDARY_COLOUR    0x00FF00CF


/**
 * @brief The kernel main program.
 * This is the kernel main entry point and its main program.
 */
void kernel_main(Boot_Info* boot_info);
static void kernel_main_stage2(Boot_Info* boot_info);

/* Port I/O functions for VGA mode setting */
static inline void outb(uint16_t port, uint8_t value) {
	__asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
	uint8_t value;
	__asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
	return value;
}

/* Clone the firmware GDT into kernel-owned memory so it remains mapped post-CR3 switch. */
struct __attribute__((packed)) gdtr {
	uint16_t limit;
	uint64_t base;
};

static uint64_t gdt_shadow[8] __attribute__((aligned(16)));
static uintptr_t g_kernel_pml4 = 0;

extern uintptr_t kernel_start;  /* from linker script */
extern uintptr_t kernel_end;

static void gdt_clone_and_load(void) {
	struct gdtr old_gdtr;
	__asm__ volatile("sgdt %0" : "=m"(old_gdtr));

	uint16_t bytes = old_gdtr.limit + 1;
	if (bytes > sizeof(gdt_shadow)) {
		bytes = sizeof(gdt_shadow);
	}
	memcpy((void*)gdt_shadow, (const void*)old_gdtr.base, bytes);

	struct gdtr new_gdtr = {
		.limit = bytes - 1,
		.base = (uint64_t)gdt_shadow,
	};
	__asm__ volatile("lgdt %0" : : "m"(new_gdtr));
}

static void gdt_reload_high(void) {
	struct gdtr new_gdtr = {
		.limit = (uint16_t)(sizeof(gdt_shadow) - 1),
		.base = (uint64_t)gdt_shadow + KERNEL_VIRT_BASE,
	};
	__asm__ volatile("lgdt %0" : : "m"(new_gdtr));
}

/**
 * @brief Write text to the framebuffer
 */
void write_text(volatile uint32_t* fb, uint32_t pitch, uint32_t width, uint32_t height,
                const char* text, uint32_t x, uint32_t y)
{
	for (int i = 0; text[i]; i++) {
		draw_char_8x16(fb, pitch, text[i], x + (i * 9), y, width, height);
	}
}


void kernel_main(Boot_Info* boot_info)
{
	uart_initialize();
	/* Send banner to serial */
	const char* banner = "\n=== KERNEL RUNNING ===\n";
	for (int i = 0; banner[i]; i++) {
		outb(0x3F8, banner[i]);
	}
	
	/* Initialize frame allocator from UEFI memory map */
	frame_alloc_init(boot_info);
	
	/* Initialize paging with identity mapping (phys == virt) */
	paging_init(0);

	/* Ensure GDT resides in kernel-owned, mapped memory before switching CR3 */
	gdt_clone_and_load();
	
	/* Install IDT and register handlers BEFORE CR3 switch (before any page faults can occur) */
	idt_install();
	
	/* Set up kernel's PML4 and switch to it */
	g_kernel_pml4 = paging_setup_kernel_pml4();
	if (!g_kernel_pml4) {
		const char* err = "ERROR: Failed to create kernel PML4\n";
		for (int i = 0; err[i]; i++) {
			outb(0x3F8, err[i]);
		}
		while (1) {
			__asm__ volatile("pause");
		}
	}

	/* Ensure boot_info and its memory map remain accessible after CR3 switch */
	const uintptr_t boot_info_phys = (uintptr_t)boot_info; /* identity assumed */
	const size_t boot_info_size = sizeof(Boot_Info);
	uintptr_t boot_info_start = boot_info_phys & ~(PAGE_SIZE - 1);
	uintptr_t boot_info_end   = (boot_info_phys + boot_info_size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
	size_t boot_info_pages = (boot_info_end - boot_info_start) / PAGE_SIZE;
	paging_map_range(g_kernel_pml4, boot_info_start, boot_info_start, boot_info_pages,
	                PTE_PRESENT | PTE_RW | PTE_GLOBAL);

	const uintptr_t mmap_phys = (uintptr_t)boot_info->memory_map;
	const size_t mmap_size = (size_t)boot_info->memory_map_size;
	uintptr_t mmap_start = mmap_phys & ~(PAGE_SIZE - 1);
	uintptr_t mmap_end   = (mmap_phys + mmap_size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
	size_t mmap_pages = (mmap_end - mmap_start) / PAGE_SIZE;
	paging_map_range(g_kernel_pml4, mmap_start, mmap_start, mmap_pages,
	                PTE_PRESENT | PTE_RW | PTE_GLOBAL);
	paging_load_cr3(g_kernel_pml4);
	
	const char* paging_ok = "Kernel PML4 loaded, CR3 switched.\n";
	for (int i = 0; paging_ok[i]; i++) {
		outb(0x3F8, paging_ok[i]);
	}

	/* If we are still executing from the low identity mapping, hop to the higher-half alias. */
	uintptr_t here;
	__asm__ volatile("lea (%%rip), %0" : "=r"(here));
	if (here < KERNEL_VIRT_BASE) {
		extern char stack_top;
		uintptr_t new_rsp = ((uintptr_t)&stack_top) + KERNEL_VIRT_BASE;
		__asm__ volatile("mov %0, %%rsp" :: "r"(new_rsp));

		void (*stage2_hh)(Boot_Info*) = (void (*)(Boot_Info*))((uintptr_t)&kernel_main_stage2 + KERNEL_VIRT_BASE);
		stage2_hh(boot_info);
		while (1) { __asm__ volatile("hlt"); }
	}

	/* Already running higher-half; continue. */
	kernel_main_stage2(boot_info);

}

static void kernel_main_stage2(Boot_Info* boot_info)
{
	/* Reload descriptor tables to their higher-half aliases before dropping identity mappings. */
	gdt_reload_high();
	idt_reload_high();
	/* Keep the identity mapping for now to avoid early faults; we already run from higher half. */

	if (boot_info->video_mode_info.framebuffer_pointer == NULL) {
		const char* no_gfx = "ERROR: No framebuffer\n";
		for (int i = 0; no_gfx[i]; i++) {
			outb(0x3F8, no_gfx[i]);
		}
		while (1) {
			__asm__ volatile("pause");
		}
	}
	
	/* Get framebuffer info */
	volatile uint32_t* fb = (uint32_t*)boot_info->video_mode_info.framebuffer_pointer;
	uint32_t width = boot_info->video_mode_info.horizontal_resolution;
	uint32_t height = boot_info->video_mode_info.vertical_resolution;
	uint32_t pitch = boot_info->video_mode_info.pixels_per_scaline;
	
	/* Clear screen to black */
	for (uint32_t y = 0; y < height; y++) {
		for (uint32_t x = 0; x < width; x++) {
			fb[y * pitch + x] = 0x00000000;  /* Black */
		}
	}
	
	/* Draw "KERNEL RUNNING" with 8x16 bitmap font */
	const char* text = "KERNEL RUNNING";
	uint32_t start_x = 100;
	uint32_t start_y = 100;
	
	write_text(fb, pitch, width, height, text, start_x, start_y);
	
	const char* done = "Framebuffer written\n";
	for (int i = 0; done[i]; i++) {
		outb(0x3F8, done[i]);
	}

	/* Initialize VGA with framebuffer */
	vga_init_fb((void*)fb, width, height, pitch);
	vga_clear(VGA_COLOR_BLACK, VGA_COLOR_LIGHT_GREY);
	
	/* Timer temporarily disabled until scheduler logic is ready */
	//timer_install();

	keyboard_init();

	scheduler_init();
	
	/* Enable interrupts */
	__asm__ volatile("sti");
	
	const char* ready = "Keyboard ready - type something!\n";
	for (int i = 0; ready[i]; i++) {
		outb(0x3F8, ready[i]);
	}
	
	vga_write("Type on keyboard: ");

	/* Infinite loop */
	while (1) {
		char c = keyboard_getchar();
		if (c) {
			vga_putc(c);
		}
		__asm__ volatile("pause");
	}
}
