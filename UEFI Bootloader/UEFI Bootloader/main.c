#include <efi.h>
#include <efilib.h>



//typedef unsigned long long UINTN;
//typedef unsigned short CHAR16;
//typedef void* EFI_HANDLE;
//typedef unsigned long long EFI_STATUS;

#define EFI_SUCCESS 0



EFI_STATUS EFIAPI efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable) {
    InitializeLib(ImageHandle, SystemTable);

    Print(L"UEFI Bootloader Started\n");

    EFI_STATUS Status;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *FileSystem;
    EFI_FILE_PROTOCOL *Root;
    EFI_FILE_PROTOCOL *KernelFile;

    // Locate the filesystem protocol
    Status = uefi_call_wrapper(BS->HandleProtocol,
        3,
        ImageHandle,
        &gEfiSimpleFileSystemProtocolGuid,
        (void**)&FileSystem);

    if (EFI_ERROR(Status)) {
        Print(L"Could not locate file system: %r\n", Status);
        return Status;
    }

    // Open the root volume
    Status = FileSystem->OpenVolume(FileSystem, &Root);
    if (EFI_ERROR(Status)) {
        Print(L"Could not open root volume: %r\n", Status);
        return Status;
    }

    // Try to open kernel.elf
    Status = Root->Open(Root,
                        &KernelFile,
                        L"kernel.elf",
                        EFI_FILE_MODE_READ,
                        0);
    if (EFI_ERROR(Status)) {
        Print(L"Failed to open kernel.elf: %r\n", Status);
        return Status;
    }

    Print(L"kernel.elf opened successfully!\n");

    // Get file info (size, etc.)
    EFI_FILE_INFO *FileInfo;
    UINTN InfoSize = SIZE_OF_EFI_FILE_INFO + 200;
    FileInfo = AllocatePool(InfoSize);

    Status = KernelFile->GetInfo(KernelFile,
                                 &gEfiFileInfoGuid,
                                 &InfoSize,
                                 FileInfo);
    if (EFI_ERROR(Status)) {
        Print(L"Could not get file info: %r\n", Status);
        return Status;
    }

    Print(L"kernel.elf size: %lu bytes\n", FileInfo->FileSize);

    // Allocate buffer for file contents
    VOID *KernelBuffer;
    Status = uefi_call_wrapper(BS->AllocatePool,
        3,
        EfiLoaderData,
        FileInfo->FileSize,
        &KernelBuffer);

    if (EFI_ERROR(Status)) {
        Print(L"Could not allocate memory for kernel: %r\n", Status);
        return Status;
    }

    // Read the file into buffer
    UINTN FileSize = FileInfo->FileSize;
    Status = KernelFile->Read(KernelFile, &FileSize, KernelBuffer);
    if (EFI_ERROR(Status)) {
        Print(L"Failed to read kernel file: %r\n", Status);
        return Status;
    }

    Print(L"kernel.elf loaded into memory at %p\n", KernelBuffer);

    // Close file and free info
    KernelFile->Close(KernelFile);
    FreePool(FileInfo);

    Print(L"Bootloader finished (dummy load only)\n");

    return EFI_SUCCESS;
}