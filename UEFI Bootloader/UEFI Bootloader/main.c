#include <efi.h>
#include <efilib.h>

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

    Print(L"Bootloader finished (dummy load only)\n");
    Print(L"Press any key to exit...\n");

    // Wait for a key press
    EFI_INPUT_KEY Key;
    while ((Status = uefi_call_wrapper(ST->ConIn->ReadKeyStroke, 2, ST->ConIn, &Key)) == EFI_NOT_READY) {
        // Wait for key
    }

    return EFI_SUCCESS;
}