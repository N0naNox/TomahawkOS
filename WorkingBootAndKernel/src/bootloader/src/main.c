/*
 * Bootloader entry point and main application.
 * The entry point for the application. Contains the main bootloader code that
 * initiates the loading of the Kernel executable. The main application is
 * contained within the `efi_main` function.
 */

#include <efi.h>
#include <efilib.h>

#include <bootloader.h>
#include <debug.h>
#include <error.h>
#include <fs.h>
#include <serial.h>
#include <memory_map.h>

#define TARGET_SCREEN_WIDTH     1024
#define TARGET_SCREEN_HEIGHT    768
#define TARGET_PIXEL_FORMAT     PixelBlueGreenRedReserved8BitPerColor


/**
 * File System Service instance.
 * Refer to definition in bootloader.h
 */
Uefi_File_System_Service file_system_service;
/**
 * Serial IO Service instance.
 * Refer to definition in bootloader.h
 */
Uefi_Serial_Service serial_service;

/**
 * Whether to draw a test pattern to video output to test the graphics output
 * service.
 */
#define DRAW_TEST_SCREEN 1


/**
 * efi_main
 */
EFI_STATUS EFIAPI efi_main(EFI_HANDLE ImageHandle,
	EFI_SYSTEM_TABLE* SystemTable)
{
	/** Main bootloader application status. */
	EFI_STATUS status;
	
	/* Try to get Graphics Output Protocol for framebuffer info */
	EFI_GRAPHICS_OUTPUT_PROTOCOL* gop = NULL;
	EFI_GRAPHICS_OUTPUT_MODE_INFORMATION* mode_info = NULL;
	/**
	 * The root file system entity.
	 * This is the file root from which the kernel binary will be loaded.
	 */
	EFI_FILE* root_file_system;
	/** The kernel entry point address. */
	/* Use a static variable so it persists after ExitBootServices */
	static EFI_PHYSICAL_ADDRESS kernel_entry_point_value = 0;
	EFI_PHYSICAL_ADDRESS* kernel_entry_point = &kernel_entry_point_value;
	/** The EFI memory map descriptor. */
	EFI_MEMORY_DESCRIPTOR* memory_map = NULL;
	/** The memory map key. */
	UINTN memory_map_key = 0;
	/** The size of the memory map buffer. */
	UINTN memory_map_size = 0;
	/** The memory map descriptor size. */
	UINTN descriptor_size;
	/** The memory map descriptor. */
	UINT32 descriptor_version;
	/** Function pointer to the kernel entry point. */
	void (*kernel_entry)(Kernel_Boot_Info* boot_info);
	/** Boot info struct, passed to the kernel. */
	Kernel_Boot_Info boot_info;
	/** Input key type used to capture user input. */
	EFI_INPUT_KEY input_key;
	/** Initrd (initial RAM disk) loading state. */
	EFI_FILE* initrd_file = NULL;
	EFI_FILE_INFO* initrd_info = NULL;
	UINTN initrd_info_size = 0;
	EFI_PHYSICAL_ADDRESS initrd_phys = 0;
	UINTN initrd_read_size = 0;
	UINTN initrd_pages = 0;

	// Initialise service protocols to NULL, so that we can detect if they are
	// properly initialised in service functions.
	serial_service.protocol = NULL;
	file_system_service.protocol = NULL;

	// Initialise the UEFI lib.
	InitializeLib(ImageHandle, SystemTable);

	// Disable the watchdog timer.
	status = uefi_call_wrapper(gBS->SetWatchdogTimer, 4, 0, 0, 0, NULL);
	if(check_for_fatal_error(status, L"Error setting watchdog timer")) {
		return status;
	}

	// Try to get Graphics Output Protocol for framebuffer
	#ifdef DEBUG
		debug_print_line(L"Debug: Attempting to locate Graphics Output Protocol\n");
	#endif
	
	status = uefi_call_wrapper(gBS->LocateProtocol, 3,
		&gEfiGraphicsOutputProtocolGuid, NULL, (VOID**)&gop);
	if (!EFI_ERROR(status) && gop != NULL) {
		#ifdef DEBUG
			debug_print_line(L"Debug: GOP located, current mode: %u\n", gop->Mode->Mode);
		#endif
		
		mode_info = gop->Mode->Info;
		#ifdef DEBUG
			debug_print_line(L"Debug: Framebuffer at 0x%llx, %ux%u\n",
				gop->Mode->FrameBufferBase,
				mode_info->HorizontalResolution,
				mode_info->VerticalResolution);
		#endif
	} else {
		#ifdef DEBUG
			debug_print_line(L"Debug: GOP not found, no graphics available\n");
		#endif
		gop = NULL;
	}

	// Reset console input.
	status = uefi_call_wrapper(ST->ConIn->Reset, 2, SystemTable->ConIn, FALSE);
	if(check_for_fatal_error(status, L"Error resetting console input")) {
		return status;
	}

	// Initialise the serial service.
	status = init_serial_service();
	if(EFI_ERROR(status)) {
		if(status == EFI_NOT_FOUND) {
			#ifdef DEBUG
				debug_print_line(L"Debug: No serial device found\n");
			#endif
		} else {
			debug_print_line(L"Fatal Error: Error initialising Serial IO service\n");

			#if PROMPT_FOR_INPUT_BEFORE_REBOOT_ON_FATAL_ERROR
				debug_print_line(L"Press any key to reboot...");
				wait_for_input(&input_key);
			#endif

			return status;
		}
	}


	// Initialise the simple file system service.
	// This will be used to load the kernel binary.
	status = init_file_system_service();
	if(EFI_ERROR(status)) {
		// Error has already been printed.
		return status;
	}

	status = uefi_call_wrapper(file_system_service.protocol->OpenVolume, 2,
		file_system_service.protocol, &root_file_system);
	if(check_for_fatal_error(status, L"Error opening root volume")) {
		return status;
	}

	#ifdef DEBUG
		debug_print_line(L"Debug: Loading Kernel image\n");
	#endif

	status = load_kernel_image(root_file_system, KERNEL_EXECUTABLE_PATH,
		kernel_entry_point);
	if(EFI_ERROR(status)) {
		// In the case that loading the kernel image failed, the error message will
		// have already been printed.
		return status;
	}

	#ifdef DEBUG
		debug_print_line(L"Debug: Set Kernel Entry Point to: '0x%llx'\n",
			*kernel_entry_point);
	#endif

	// Set video mode info from GOP if available
	if (gop != NULL && mode_info != NULL) {
		boot_info.video_mode_info.framebuffer_pointer = (VOID*)gop->Mode->FrameBufferBase;
		boot_info.video_mode_info.horizontal_resolution = mode_info->HorizontalResolution;
		boot_info.video_mode_info.vertical_resolution = mode_info->VerticalResolution;
		boot_info.video_mode_info.pixels_per_scaline = mode_info->PixelsPerScanLine;
		
		#ifdef DEBUG
			debug_print_line(L"Debug: Video info passed to kernel\n");
		#endif
	} else {
		// No graphics available, provide zeros
		boot_info.video_mode_info.framebuffer_pointer = (VOID*)0;
		boot_info.video_mode_info.horizontal_resolution = 0;
		boot_info.video_mode_info.vertical_resolution = 0;
		boot_info.video_mode_info.pixels_per_scaline = 0;
		
		#ifdef DEBUG
			debug_print_line(L"Debug: No graphics available\n");
		#endif
	}

	#ifdef DEBUG
		debug_print_line(L"Debug: Closing Graphics Output Service handles\n");
	#endif


	/* ---- Load initrd.img into memory ---- */
	boot_info.initrd_base = 0;
	boot_info.initrd_size = 0;

	status = uefi_call_wrapper(root_file_system->Open, 5,
		root_file_system, &initrd_file, INITRD_PATH,
		EFI_FILE_MODE_READ, EFI_FILE_READ_ONLY);
	if (!EFI_ERROR(status) && initrd_file != NULL) {
		/* Get file size via GetInfo */
		initrd_info_size = sizeof(EFI_FILE_INFO) + 256;
		status = uefi_call_wrapper(gBS->AllocatePool, 3,
			EfiLoaderData, initrd_info_size, (VOID**)&initrd_info);
		if (!EFI_ERROR(status)) {
			status = uefi_call_wrapper(initrd_file->GetInfo, 4,
				initrd_file, &gEfiFileInfoGuid,
				&initrd_info_size, (VOID*)initrd_info);
			if (!EFI_ERROR(status)) {
				initrd_read_size = (UINTN)initrd_info->FileSize;
				initrd_pages     = EFI_SIZE_TO_PAGES(initrd_read_size);
				status = uefi_call_wrapper(gBS->AllocatePages, 4,
					AllocateAnyPages, EfiLoaderData,
					initrd_pages, &initrd_phys);
				if (!EFI_ERROR(status)) {
					status = uefi_call_wrapper(initrd_file->Read, 3,
						initrd_file, &initrd_read_size, (VOID*)initrd_phys);
					if (!EFI_ERROR(status)) {
						boot_info.initrd_base = initrd_phys;
						boot_info.initrd_size = (UINTN)initrd_info->FileSize;
					} else {
						/* Read failed — free pages and leave fields 0 */
						uefi_call_wrapper(gBS->FreePages, 2, initrd_phys, initrd_pages);
					}
				}
			}
			uefi_call_wrapper(gBS->FreePool, 1, initrd_info);
		}
		uefi_call_wrapper(initrd_file->Close, 1, initrd_file);
	}
	/* (If initrd is absent or load failed, boot_info.initrd_base/size remain 0) */

	#ifdef DEBUG
		debug_print_line(L"Debug: Getting memory map and exiting boot services\n");
	#endif

	// Get the memory map prior to exiting the boot service.
	status = get_memory_map((VOID**)&memory_map, &memory_map_size,
		&memory_map_key, &descriptor_size, &descriptor_version);
	if(EFI_ERROR(status)) {
		// Error has already been printed.
		return status;
	}

	#ifdef DEBUG
		debug_print_line(L"Debug: About to print memory map\n");
	#endif

	debug_print_memory_map(memory_map, memory_map_size, descriptor_size);

	#ifdef DEBUG
		debug_print_line(L"Debug: Finished printing memory map, calling ExitBootServices\n");
	#endif

	// ExitBootServices requires the memory_map_key from the most recent GetMemoryMap call
	// The memory_map and its key are valid and can be used now

	#ifdef DEBUG
		debug_print_line(L"Debug: Calling ExitBootServices with key 0x%llx\n", memory_map_key);
		debug_print_line(L"Debug: Kernel entry address (raw): 0x%llx\n", *kernel_entry_point);
		debug_print_line(L"Debug: Boot info address: 0x%llx\n", &boot_info);
	#endif

	status = uefi_call_wrapper(gBS->ExitBootServices, 2,
		ImageHandle, memory_map_key);
	
	// NOTE: Cannot print debug output after ExitBootServices - boot services are gone!
	// The next lines will jump directly to the kernel without any debug output
	
	if(EFI_ERROR(status)) {
		// If ExitBootServices failed, we're in an undefined state
		// We can't print errors or return, just halt
		__asm__ volatile("cli; hlt");
		return status;  // Never reached
	}

	// Set kernel boot info.
	boot_info.memory_map = memory_map;
	boot_info.memory_map_size = memory_map_size;
	boot_info.memory_map_descriptor_size = descriptor_size;

	// Cast pointer to kernel entry.
	kernel_entry = (void (*)(Kernel_Boot_Info*))*kernel_entry_point;
	// Jump to kernel entry.
	kernel_entry(&boot_info);

	// Return an error if this code is ever reached.
	return EFI_LOAD_ERROR;
}


/**
 * wait_for_input
 */
EFI_STATUS wait_for_input(OUT EFI_INPUT_KEY* key) {
	/** The program status. */
	EFI_STATUS status;
	do {
		status = uefi_call_wrapper(ST->ConIn->ReadKeyStroke, 2, ST->ConIn, key);
	} while(status == EFI_NOT_READY);

	return status;
}
