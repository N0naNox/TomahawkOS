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
#include "include/syscall_init.h"
#include "include/syscall.h"
#include "include/syscall_numbers.h"
#include "include/gdt.h"
#include "include/refcount.h"

/* Demo threads and helpers */
static void demo_thread_a(void);
static void demo_thread_b(void);
static void demo_esc_watcher(void);
static void menu_thread(void);
static void idle_thread(void);
static void keyboard_flush(void);

typedef enum {
	DEMO_ECHO = 1,
	DEMO_SCHED = 2,
	DEMO_USERMODE = 3
} demo_mode_t;

static demo_mode_t select_demo(void);
static void run_echo_demo(void);
static void run_scheduler_demo(void);
static void demo_busywait(void);

static volatile int demo_stop_requested = 0;

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



static uintptr_t g_kernel_pml4 = 0;

extern uintptr_t kernel_start;  /* from linker script */
extern uintptr_t kernel_end;





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
	
	/* Initialize reference counting for COW (track up to 2M frames = 8GB) */
	refcount_init(2 * 1024 * 1024);
	
	/* Initialize paging with identity mapping (phys == virt) */
	paging_init(0);

	gdt_init();
	
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
	gdt_init();
	idt_reload_high();
	/* Keep the identity mapping for now to avoid early faults; we already run from higher half. */

	/* Initialize user syscall machinery */
	syscall_init();

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
	
	/* Install PIT timer for preemptive scheduling */
	timer_install();

	keyboard_init();

	scheduler_init();

	/* Create essential threads */
	create_process("idle", idle_thread);
	create_process("menu", menu_thread);

	/* Enable interrupts and let the scheduler take over */
	__asm__ volatile("sti");

	/* Yield from bootstrap context into first scheduled thread */
	while (1) {
		__asm__ volatile("hlt");
	}
}

static demo_mode_t select_demo(void) {
	while (1) {
		char c = keyboard_getchar();
		if (c == '1') {
			return DEMO_ECHO;
		} 

		else if (c == '3') {
			return DEMO_USERMODE;
		}
		
		else if (c == '2') {
			return DEMO_SCHED;
		}
	}
}

static void demo_busywait(void) {
	for (volatile int i = 0; i < 2000000; i++) {
		__asm__ volatile("pause");
	}
}

static void demo_thread_a(void) {
	while (!demo_stop_requested) {
		uart_puts("[thread A] hello\n");
		demo_busywait();
	}
	scheduler_thread_exit();
}

static void demo_thread_b(void) {
	while (!demo_stop_requested) {
		uart_puts("[thread B] hello\n");
		demo_busywait();
	}
	scheduler_thread_exit();
}

static void demo_esc_watcher(void) {
	while (!demo_stop_requested) {
		char c = keyboard_getchar();
		if (c == 27) { /* ESC */
			demo_stop_requested = 1;
			break;
		}
		__asm__ volatile("pause");
	}
	scheduler_thread_exit();
}

static void idle_thread(void) {
	for (;;) {
		__asm__ volatile("hlt");
	}
}

static void run_echo_demo(void) {
	vga_write("Starting keyboard echo demo (ESC to stop)...\n");
	uart_puts("Starting keyboard echo demo (ESC to stop)...\n");

	while (1) {
		char c = keyboard_getchar();
		if (c == 27) { /* ESC */
			break;
		}
		if (c) {
			vga_putc(c);
		}
		__asm__ volatile("pause");
	}
}

static void run_scheduler_demo(void) {
	vga_write("Starting scheduler demo (ESC to stop)...\n");
	uart_puts("Starting scheduler demo (ESC to stop)...\n");

	demo_stop_requested = 0;
	create_process("esc-watcher", demo_esc_watcher);
	create_process("demo-a", demo_thread_a);
	create_process("demo-b", demo_thread_b);

	/* Wait until ESC is pressed */
	while (!demo_stop_requested) {
		__asm__ volatile("pause");
	}

	vga_write("Stopping scheduler demo...\n");
	uart_puts("Stopping scheduler demo...\n");
	/* Allow demo threads to notice stop flag and exit */
	for (volatile int i = 0; i < 5000000; i++) {
		__asm__ volatile("pause");
	}
}

static void menu_thread(void) {
	for (;;) {
		keyboard_flush();
		/* Clear screen and prompt */
		vga_clear(VGA_COLOR_BLACK, VGA_COLOR_LIGHT_GREY);
		uart_puts("Select demo: 1=echo, 2=scheduler\n");
		vga_write("Select demo:\n");
		vga_write("  1) Keyboard echo\n");
		vga_write("  2) Scheduler threads (ESC to stop)\n");
		vga_write("  3) Jump to User Mode (Ring 3 Test)\n");
		vga_write("Press 1 or 2 or 3...\n");

		demo_mode_t mode = select_demo();

		if (mode == DEMO_SCHED) {
			run_scheduler_demo();
		
		} 

		else if (mode == DEMO_USERMODE) {
			vga_write("Jumping to User Mode demo...\n");
			uart_puts("Jumping to User Mode demo...\n");
			start_user_demo();
		}
		
		else {
			run_echo_demo();
		}

		keyboard_flush();
	}
}

static void keyboard_flush(void) {
	while (keyboard_getchar()) {
		/* discard */
	}
}


void user_code_entry() {
    while(1) {
        // הדפסה
        __asm__ volatile(
            "mov $1, %%rax; mov %0, %%rdi; syscall" 
            : : "r"("User is yielding...\n") : "rax", "rdi", "rcx", "r11"
        );

        // קריאה ל-Yield
        __asm__ volatile(
            "mov $3, %%rax; syscall" 
            : : : "rax", "rcx", "r11"
        );
    }
}



void start_user_demo() {
    static uint8_t user_stack[8192] __attribute__((aligned(4096)));
    uint64_t user_stack_bottom = (uint64_t)user_stack;
    uint64_t user_stack_top = user_stack_bottom + sizeof(user_stack);

    // פתיחת הרשאות ל-Stack (מכסים 2 דפים לביטחון)
    paging_set_user_bit(user_stack_bottom, 1);
    paging_set_user_bit(user_stack_bottom + 4096, 1);

    // פתיחת הרשאות לקוד
    // בגלל שזה בתוך הקרנל, אנחנו פותחים את הביט לדף שבו הפונקציה נמצאת
    paging_set_user_bit((uintptr_t)user_code_entry, 1);
    
    // אם הפונקציה ארוכה, כדאי לפתוח גם את הדף הבא
    paging_set_user_bit((uintptr_t)user_code_entry + 4096, 1);

    vga_write("Paging updated. Jumping to Ring 3...\n");
    jump_to_user((uint64_t)user_code_entry, user_stack_top);
}


