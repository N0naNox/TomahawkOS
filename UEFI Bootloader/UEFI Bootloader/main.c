// loader.c - simple UEFI ELF64 loader (minimal, educational)
// Builds with GNU-EFI. See Makefile for compile commands.
// This loader:
//  - reads kernel.elf from the same volume
//  - pre-allocates memmap buffer (to avoid pool reuse clobbering code pages)
//  - allocates pages with EfiLoaderCode/EfiLoaderData based on PT flags
//  - copies PT_LOAD segments
//  - allocates a magic pointer and prints it
//  - final GetMemoryMap -> ExitBootServices
//  - jumps to kernel with rdi/rsi/rdx set (magic, fb_base, fb_pitch)

#include <efi.h>
#include <efilib.h>
#include <stddef.h>
#include <stdint.h>

#define PF_X 1

UINT64* g_magic_ptr = NULL;

typedef uint16_t Elf64_Half;
typedef uint32_t Elf64_Word;
typedef int32_t  Elf64_Sword;
typedef uint64_t Elf64_Xword;
typedef int64_t  Elf64_Sxword;
typedef uint64_t Elf64_Addr;
typedef uint64_t Elf64_Off;

#define EI_NIDENT 16

typedef struct {
    unsigned char e_ident[EI_NIDENT];
    Elf64_Half    e_type;
    Elf64_Half    e_machine;
    Elf64_Word    e_version;
    Elf64_Addr    e_entry;
    Elf64_Off     e_phoff;
    Elf64_Off     e_shoff;
    Elf64_Word    e_flags;
    Elf64_Half    e_ehsize;
    Elf64_Half    e_phentsize;
    Elf64_Half    e_phnum;
    Elf64_Half    e_shentsize;
    Elf64_Half    e_shnum;
    Elf64_Half    e_shstrndx;
} Elf64_Ehdr;

typedef struct {
    Elf64_Word p_type;
    Elf64_Word p_flags;
    Elf64_Off  p_offset;
    Elf64_Addr p_vaddr;
    Elf64_Addr p_paddr;
    Elf64_Xword p_filesz;
    Elf64_Xword p_memsz;
    Elf64_Xword p_align;
} Elf64_Phdr;

#define PT_LOAD 1

EFI_STATUS
efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE* SystemTable) {
    EFI_STATUS Status;
    InitializeLib(ImageHandle, SystemTable);
    Print(L"Simple ELF64 UEFI loader\n");

    // Locate GOP
    EFI_GRAPHICS_OUTPUT_PROTOCOL* Gop = NULL;
    Status = uefi_call_wrapper(BS->LocateProtocol, 3, &gEfiGraphicsOutputProtocolGuid, NULL, (void**)&Gop);
    if (EFI_ERROR(Status)) {
        Print(L"GOP not found: %r\n", Status);
    }
    else {
        Print(L"GOP: fb=0x%lx w=%u h=%u pitch=%u fmt=%u\n",
            (UINT64)(UINTN)Gop->Mode->FrameBufferBase,
            Gop->Mode->Info->HorizontalResolution,
            Gop->Mode->Info->VerticalResolution,
            Gop->Mode->Info->PixelsPerScanLine,
            Gop->Mode->Info->PixelFormat);
    }

    // Open the kernel file (kernel.elf) from the current volume
    EFI_FILE_IO_INTERFACE* Vol = NULL;
    EFI_FILE_HANDLE Root = NULL;
    EFI_FILE_HANDLE File = NULL;
    EFI_LOADED_IMAGE* LoadedImage;
    Status = uefi_call_wrapper(BS->HandleProtocol, 3, ImageHandle, &gEfiLoadedImageProtocolGuid, (void**)&LoadedImage);
    if (EFI_ERROR(Status)) {
        Print(L"HandleProtocol(LoadedImage) failed: %r\n", Status);
        return Status;
    }
    Status = uefi_call_wrapper(BS->HandleProtocol, 3, LoadedImage->DeviceHandle, &gEfiSimpleFileSystemProtocolGuid, (void**)&Vol);
    if (EFI_ERROR(Status)) {
        Print(L"HandleProtocol(SimpleFS) failed: %r\n", Status);
        return Status;
    }
    Status = uefi_call_wrapper(Vol->OpenVolume, 2, Vol, &Root);
    if (EFI_ERROR(Status)) {
        Print(L"OpenVolume failed: %r\n", Status);
        return Status;
    }

    Status = uefi_call_wrapper(Root->Open, 5, Root, &File, L"\\kernel.elf", EFI_FILE_MODE_READ, 0);
    if (EFI_ERROR(Status)) {
        Print(L"Open kernel.elf failed: %r\n", Status);
        return Status;
    }

    // Get file size
    EFI_FILE_INFO* FileInfo = LibFileInfo(File);
    if (!FileInfo) {
        Print(L"LibFileInfo failed\n");
        return EFI_LOAD_ERROR;
    }
    UINTN FileSize = (UINTN)FileInfo->FileSize;
    FreePool(FileInfo);

    // Read file into buffer
    VOID* FileBuffer = NULL;
    Status = uefi_call_wrapper(BS->AllocatePool, 3, EfiLoaderData, FileSize, (void**)&FileBuffer);
    if (EFI_ERROR(Status) || !FileBuffer) {
        Print(L"AllocatePool(FileBuffer) failed: %r\n", Status);
        return Status;
    }
    UINTN ReadSize = FileSize;
    Status = uefi_call_wrapper(File->Read, 3, File, &ReadSize, FileBuffer);
    if (EFI_ERROR(Status)) {
        Print(L"Read kernel.elf failed: %r\n", Status);
        return Status;
    }
    Print(L"Read kernel.elf size=%u\n", FileSize);

    // --- PRE-ALLOCATE memory map buffer to avoid pool re-use clobbering kernel pages ---
    EFI_MEMORY_DESCRIPTOR* memmap = NULL;
    UINTN map_size = 0;
    UINTN map_key = 0;
    UINTN desc_size = 0;
    UINT32 desc_version = 0;
    Status = uefi_call_wrapper(BS->GetMemoryMap, 5, &map_size, memmap, &map_key, &desc_size, &desc_version);
    if (Status == EFI_BUFFER_TOO_SMALL) {
        // allocate some slack
        map_size += desc_size * 4;
        Status = uefi_call_wrapper(BS->AllocatePool, 3, EfiLoaderData, map_size, (void**)&memmap);
        if (EFI_ERROR(Status) || !memmap) {
            Print(L"AllocatePool(memmap) failed: %r\n", Status);
            return Status;
        }
        Print(L"Preallocated memmap buffer of %u bytes\n", map_size);
    }
    else if (EFI_ERROR(Status)) {
        Print(L"GetMemoryMap(initial) failed: %r\n", Status);
        return Status;
    }

    // Parse ELF header
    if (FileSize < sizeof(Elf64_Ehdr)) {
        Print(L"kernel.elf too small\n");
        return EFI_LOAD_ERROR;
    }
    Elf64_Ehdr* ehdr = (Elf64_Ehdr*)FileBuffer;
    Elf64_Phdr* phdrs = (Elf64_Phdr*)((UINT8*)FileBuffer + ehdr->e_phoff);

    UINT64 load_bias = 0;
    UINTN phnum = ehdr->e_phnum;
    Print(L"ELF phnum=%u entry=%lx\n", phnum, (UINT64)ehdr->e_entry);

    // Load PT_LOAD segments
    for (UINTN i = 0; i < phnum; ++i) {
        Elf64_Phdr* ph = &phdrs[i];
        if (ph->p_type != PT_LOAD) continue;

        // compute pages needed
        UINTN memsz = (UINTN)ph->p_memsz;
        UINTN filesz = (UINTN)ph->p_filesz;
        UINTN paddr_pages = (memsz + 0xFFF) / 0x1000;

        // Allocate pages with appropriate memory type
        EFI_MEMORY_TYPE mem_type = (ph->p_flags & PF_X) ? EfiLoaderCode : EfiLoaderData;
        EFI_PHYSICAL_ADDRESS dest = 0;
        Status = uefi_call_wrapper(BS->AllocatePages, 4, AllocateAnyPages, mem_type, paddr_pages, &dest);
        if (EFI_ERROR(Status)) {
            Print(L"AllocatePages failed: %r\n", Status);
            return Status;
        }

        // Copy file contents into allocated pages (zero remaining)
        void* dst_ptr = (void*)(UINTN)dest;
        void* src_ptr = (void*)((UINT8*)FileBuffer + ph->p_offset);
        if (filesz) {
            CopyMem(dst_ptr, src_ptr, filesz);
        }
        if (memsz > filesz) {
            SetMem((UINT8*)dst_ptr + filesz, memsz - filesz, 0);
        }

        Print(L"Loaded segment %u -> dest=0x%lx memsz=%u filesz=%u mem_type=%d\n",
            i, (UINT64)dest, memsz, filesz, mem_type);

        // Debug: dump first 8 bytes at dest (if filesz >= 8)
        if (filesz >= 8) {
            UINT64 val = *(UINT64*)(UINTN)dst_ptr;
            Print(L"DEBUG: first8 dst=0x%016lx\n", val);
        }

        // set load_bias to the lowest dest encountered
        if (!load_bias || dest < load_bias) load_bias = dest;
    }

    UINTN actual_entry = (UINTN)(load_bias + ehdr->e_entry);
    Print(L"DEBUG: load_bias = 0x%lx\n", (UINT64)load_bias);
    Print(L"DEBUG: actual entry = 0x%lx\n", (UINT64)actual_entry);

    // Allocate the magic pointer (while Boot Services are available)
    Status = uefi_call_wrapper(BS->AllocatePool, 3, EfiLoaderData, sizeof(UINT64), (void**)&g_magic_ptr);
    if (EFI_ERROR(Status) || !g_magic_ptr) {
        Print(L"AllocatePool(magic) failed: %r\n", Status);
        return Status;
    }
    *g_magic_ptr = 0;
    Print(L"DEBUG: g_magic_ptr = 0x%lx\n", (UINT64)(UINTN)g_magic_ptr);

    // Before final GetMemoryMap: print a few entry bytes
    UINT64 before_map_entry = *(UINT64*)(UINTN)actual_entry;
    Print(L"DEBUG: entry bytes BEFORE GetMemoryMap = 0x%016lx\n", before_map_entry);

    // Final GetMemoryMap into pre-allocated buffer
    UINTN real_map_size = map_size;
    Status = uefi_call_wrapper(BS->GetMemoryMap, 5, &real_map_size, memmap, &map_key, &desc_size, &desc_version);
    if (EFI_ERROR(Status)) {
        Print(L"GetMemoryMap (final) failed: %r\n", Status);
        return Status;
    }

    // ExitBootServices
    Status = uefi_call_wrapper(BS->ExitBootServices, 2, ImageHandle, map_key);
    if (EFI_ERROR(Status)) {
        Print(L"ExitBootServices failed: %r\n", Status);
        return Status;
    }

    // After ExitBootServices: jump to kernel with registers loaded exactly
    UINTN entry = (UINTN)actual_entry;
    UINTN magic = (UINTN)g_magic_ptr;
    UINTN fb = (UINTN)(UINTN)Gop->Mode->FrameBufferBase;
    UINTN pitch = (UINTN)Gop->Mode->Info->PixelsPerScanLine;

    // Jump to kernel: (magic, fb, pitch)
    __asm__ __volatile__(
        "mov %0, %%rdi\n\t"
        "mov %1, %%rsi\n\t"
        "mov %2, %%rdx\n\t"
        "jmp *%3\n\t"
        :
    : "r"(magic), "r"(fb), "r"(pitch), "r"(entry)
        : "rdi", "rsi", "rdx"
        );

    // Should never return
    while (1) __asm__ __volatile__("hlt");
    return EFI_SUCCESS;
}
