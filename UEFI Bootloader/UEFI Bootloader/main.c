typedef unsigned long long UINTN;
typedef unsigned short CHAR16;
typedef void* EFI_HANDLE;
typedef unsigned long long EFI_STATUS;

#define EFI_SUCCESS 0
#define EFIAPI __cdecl  // MSVC calling convention

typedef struct {
    void* Reset;
    unsigned long (*ReadKeyStroke)(struct _EFI_SIMPLE_TEXT_INPUT_PROTOCOL* This, void* Key);
    void* WaitForKey;
} EFI_SIMPLE_TEXT_INPUT_PROTOCOL;

typedef struct {
    void* Reset;
    unsigned long (*OutputString)(struct _EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL* This, CHAR16* String);
    void* TestString;
    void* QueryMode;
    void* SetMode;
    void* SetAttribute;
    void* ClearScreen;
    void* SetCursorPosition;
    void* EnableCursor;
    void* Mode;
} EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL;

typedef struct {
    char _pad[60];
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL* ConOut;
    EFI_SIMPLE_TEXT_INPUT_PROTOCOL* ConIn;
} EFI_SYSTEM_TABLE;

EFI_STATUS EFIAPI efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE* SystemTable) {
    // Print message
    SystemTable->ConOut->OutputString(SystemTable->ConOut, L"Hello, UEFI World!\r\n");

    // Wait for key
    struct { unsigned short ScanCode; CHAR16 UnicodeChar; } Key;
    SystemTable->ConIn->ReadKeyStroke(SystemTable->ConIn, &Key);

    return EFI_SUCCESS;
}