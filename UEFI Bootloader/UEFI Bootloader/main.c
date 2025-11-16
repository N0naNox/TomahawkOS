#include <efi.h>
#include <efilib.h>
#include "elf.h"

EFI_STATUS EFIAPI efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE* SystemTable) {
    EFI_STATUS Status;

    InitializeLib(ImageHandle, SystemTable);

    Print(L"UEFI Bootloader Started yoooooouuuuu\n");


    //Get loaded-image protocol (this tells us what device we were loaded from)
    EFI_LOADED_IMAGE_PROTOCOL* LoadedImage = NULL;
    Status = uefi_call_wrapper(BS->HandleProtocol, 3,
        ImageHandle,
        &gEfiLoadedImageProtocolGuid,
        (void**)&LoadedImage);
    if (EFI_ERROR(Status)) {
        Print(L"Failed to get LoadedImageProtocol: %r\n", Status);
        return Status;
    }

    Print(L"Got loaded image protocol\n");

    
    // Get Simple File System Protocol from that device
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL* FileSystem = NULL;
    Status = uefi_call_wrapper(BS->HandleProtocol, 3,
        LoadedImage->DeviceHandle,
        &gEfiSimpleFileSystemProtocolGuid,
        (void**)&FileSystem);
    if (EFI_ERROR(Status)) {
        Print(L"Could not locate file system on device: %r\n", Status);
        return Status;
    }

    Print(L"Got file system (from LoadedImage->DeviceHandle)\n");

    
    // Open the root directory
    EFI_FILE_PROTOCOL* Root = NULL;
    Status = uefi_call_wrapper(FileSystem->OpenVolume, 2, FileSystem, &Root);
    Print(L"OpenVolume() returned: %r\n", Status);
    if (EFI_ERROR(Status)) {
        return Status;
    }

    Print(L"Opened root volume\n");


    // Open kernel.elf
    EFI_FILE_PROTOCOL* KernelFile = NULL;
    Status = uefi_call_wrapper(Root->Open, 5,
        Root,
        &KernelFile,
        L"kernel.elf",
        EFI_FILE_MODE_READ,
        0);
    if (EFI_ERROR(Status)) {
        Print(L"Failed to open kernel.elf: %r\n", Status);
        return Status;
    }

    Print(L"Opened kernel.elf!\n");

    
    //Get file info
    UINTN InfoSize = SIZE_OF_EFI_FILE_INFO + 200;
    EFI_FILE_INFO* FileInfo = AllocatePool(InfoSize);
    if (!FileInfo) {
        Print(L"AllocatePool failed for FileInfo\n");
        uefi_call_wrapper(KernelFile->Close, 1, KernelFile);
        return EFI_OUT_OF_RESOURCES;
    }

    Status = uefi_call_wrapper(KernelFile->GetInfo, 4,
        KernelFile,
        &gEfiFileInfoGuid,
        &InfoSize,
        FileInfo);
    if (EFI_ERROR(Status)) {
        Print(L"Could not get file info: %r\n", Status);
        uefi_call_wrapper(KernelFile->Close, 1, KernelFile);
        FreePool(FileInfo);
        return Status;
    }

    Print(L"kernel.elf size: %llu bytes\n", (unsigned long long)FileInfo->FileSize);

    
    //Step 6: Allocate buffer and read file
    VOID* KernelBuffer = NULL;
    Status = uefi_call_wrapper(BS->AllocatePool,
        3,
        EfiLoaderData,
        (UINTN)FileInfo->FileSize,
        &KernelBuffer);
    if (EFI_ERROR(Status) || KernelBuffer == NULL) {
        Print(L"Could not allocate memory for kernel: %r\n", Status);
        uefi_call_wrapper(KernelFile->Close, 1, KernelFile);
        FreePool(FileInfo);
        return Status;
    }

    UINTN FileSize = (UINTN)FileInfo->FileSize;
    Status = uefi_call_wrapper(KernelFile->Read, 3, KernelFile, &FileSize, KernelBuffer);
    if (EFI_ERROR(Status)) {
        Print(L"Failed to read kernel file: %r\n", Status);
        uefi_call_wrapper(KernelFile->Close, 1, KernelFile);
        FreePool(FileInfo);
        FreePool(KernelBuffer);
        return Status;
    }

    Print(L"kernel.elf loaded into memory at %p (size %llu)\n", KernelBuffer, (unsigned long long)FileSize);

    uefi_call_wrapper(KernelFile->Close, 1, KernelFile);
    FreePool(FileInfo);


	Elf64_Ehdr* elfHeader = (Elf64_Ehdr*)KernelBuffer;

    if (elfHeader->e_ident[0] != 0x7F ||
        elfHeader->e_ident[1] != 'E' ||
        elfHeader->e_ident[2] != 'L' ||
        elfHeader->e_ident[3] != 'F') {
        Print(L"ERROR: Not a valid ELF file!\n");
        FreePool(KernelBuffer);
        return EFI_INVALID_PARAMETER;
    }


    Print(L"Valid ELF file detected\n");
    Print(L"Entry point: 0x%llx\n", elfHeader->e_entry);
    Print(L"Program headers: %d at offset 0x%llx\n", elfHeader->e_phnum, elfHeader->e_phoff);


    //Load the program headers of the kernel - actual code
    Elf64_Phdr* programHeaders = (Elf64_Phdr*)((UINT8*)KernelBuffer + elfHeader->e_phoff);
    for (UINT16 i = 0; i < elfHeader->e_phnum; i++) {
        Elf64_Phdr* phdr = &programHeaders[i];

        if (phdr->p_type == PT_LOAD) {  // Only load LOAD segments
            void* source = (void*)((UINT8*)KernelBuffer + phdr->p_offset);
            void* dest = (void*)phdr->p_paddr;  // or p_vaddr if using virtual addressing
            
            // Allocate pages at the destination address
            UINTN pages = (phdr->p_memsz + 4095) / 4096;  
            Status = uefi_call_wrapper(BS->AllocatePages, 4,
                AllocateAddress,
                EfiLoaderData,
                pages,
                (EFI_PHYSICAL_ADDRESS*)&dest);

            if (EFI_ERROR(Status)) {
                Print(L"Failed to allocate pages at 0x%llx: %r\n", phdr->p_paddr, Status);
                // Try allocating anywhere
                Status = uefi_call_wrapper(BS->AllocatePages, 4,
                    AllocateAnyPages,
                    EfiLoaderData,
                    pages,
                    (EFI_PHYSICAL_ADDRESS*)&dest);
                if (EFI_ERROR(Status)) {
                    Print(L"Failed to allocate pages: %r\n", Status);
                    FreePool(KernelBuffer);
                    return Status;
                }
                Print(L"Allocated at 0x%p instead\n", dest);
            }

            CopyMem(dest, source, phdr->p_filesz);

            // Zero out any remaining space (BSS section)
            if (phdr->p_memsz > phdr->p_filesz) {
                SetMem((UINT8*)dest + phdr->p_filesz, phdr->p_memsz - phdr->p_filesz, 0);
            }

            Print(L"Segment %d loaded successfully\n", i);
        }
    }

    //Obtain a memory map
    UINTN MemoryMapSize = 0;
    EFI_MEMORY_DESCRIPTOR* MemoryMap = NULL;
    UINTN MapKey;
    UINTN DescriptorSize;
    UINT32 DescriptorVersion;


    Status = uefi_call_wrapper(BS->GetMemoryMap, 5,
        &MemoryMapSize,
        MemoryMap,
        &MapKey,
        &DescriptorSize,
        &DescriptorVersion);

    if (Status == EFI_BUFFER_TOO_SMALL) {

		MemoryMapSize += DescriptorSize * 2; // Add some extra space
		Status = uefi_call_wrapper(BS->AllocatePool, 3,
			EfiLoaderData,
			MemoryMapSize,
			(void**)&MemoryMap);
		if (EFI_ERROR(Status)) {
			Print(L"Failed to allocate memory for memory map: %r\n", Status);
			FreePool(KernelBuffer);
			return Status;
		}
		Status = uefi_call_wrapper(BS->GetMemoryMap, 5,
			&MemoryMapSize,
			MemoryMap,
			&MapKey,
			&DescriptorSize,
			&DescriptorVersion);
		if (EFI_ERROR(Status)) {
			Print(L"Failed to get memory map: %r\n", Status);
			FreePool(MemoryMap);
			FreePool(KernelBuffer);
			return Status;
		}
	}
	else if (EFI_ERROR(Status)) {
		Print(L"Failed to get memory map size: %r\n", Status);
		FreePool(KernelBuffer);
		return Status;
    
    }

    Print(L"Memory map obtained, size: %llu bytes\n", (unsigned long long)MemoryMapSize);


    Print(L"Exiting boot services...\n");
    Status = uefi_call_wrapper(BS->ExitBootServices, 2, ImageHandle, MapKey);
    if (EFI_ERROR(Status)) {
        Print(L"Failed to exit boot services: %r\n", Status);
        Print(L"This is expected - memory map may have changed.\n");

        // Memory map changed, get it again
        Status = uefi_call_wrapper(BS->GetMemoryMap, 5,
            &MemoryMapSize,
            MemoryMap,
            &MapKey,
            &DescriptorSize,
            &DescriptorVersion);

        if (!EFI_ERROR(Status)) {
            Status = uefi_call_wrapper(BS->ExitBootServices, 2, ImageHandle, MapKey);
        }

        if (EFI_ERROR(Status)) {
            Print(L"Still failed to exit boot services: %r\n", Status);
            FreePool(MemoryMap);
            FreePool(KernelBuffer);
            return Status;
        }
    }


    uint64_t entry = elfHeader->e_entry;

    Print(L"Jumping to kernel at 0x%llx\n", entry);

    void (*kernel_main)(void) = (void(*)(void))entry;

    kernel_main();

	//if for some reason kernel returns, halt

    while (1) {
        __asm__("hlt");
    }


    return EFI_SUCCESS;
}