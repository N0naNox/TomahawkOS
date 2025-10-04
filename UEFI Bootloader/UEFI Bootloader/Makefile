# Root of your gnu-efi unpack
EFIROOT  = /mnt/c/Users/Cyber_User/Downloads/gnu-efi-3.0.18
EFIINC   = $(EFIROOT)/inc
EFIX64   = $(EFIROOT)/x86_64

TARGET   = BOOTX64.EFI
OBJS     = main.o

CFLAGS  = -I$(EFIINC) -I$(EFIINC)/x86_64 \
          -fpic -fshort-wchar -mno-red-zone -Wall -Wextra \
          -fno-builtin -ffreestanding -fno-stack-protector -U_FORTIFY_SOURCE

LDSCRIPT = $(EFIROOT)/gnuefi/elf_x86_64_efi.lds
CRT0     = $(EFIX64)/gnuefi/crt0-efi-x86_64.o
LIBS     = $(EFIX64)/lib/libefi.a $(EFIX64)/gnuefi/libgnuefi.a

all: $(TARGET)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

BOOTX64.so: $(OBJS)
	ld -nostdlib -znocombreloc -shared -Bsymbolic \
	   -T $(LDSCRIPT) $(CRT0) $(OBJS) -o $@ $(LIBS)

$(TARGET): BOOTX64.so
	objcopy -j .text -j .sdata -j .data -j .rodata \
	        -j .dynamic -j .dynsym -j .rel -j .rela \
	        -j .rel.* -j .rela.* -j .reloc \
	        --target=efi-app-x86_64 --subsystem=10 $< $@

clean:
	rm -f *.o *.so *.EFI
