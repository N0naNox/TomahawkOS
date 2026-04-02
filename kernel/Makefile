CC = x86_64-elf-gcc
LD = x86_64-elf-ld

CFLAGS = -fPIC -ffreestanding -mno-red-zone -fno-stack-protector -O0 -g -Wall -Wextra
LDFLAGS = -T linker.ld -nostdlib

OBJS = kernel.o

all: kernel.elf

kernel.elf: $(OBJS) linker.ld
	$(LD) $(LDFLAGS) -o kernel.elf $(OBJS)

kernel.o: kernel.c
	$(CC) $(CFLAGS) -c kernel.S -o kernel.o

clean:
	rm -f *.o kernel.elf
