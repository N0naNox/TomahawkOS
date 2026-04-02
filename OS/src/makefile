.POSIX:
.DELETE_ON_ERROR:
MAKEFLAGS += --warn-undefined-variables
MAKEFLAGS += --no-builtin-rules

KERNEL_DIR        := kernel
KERNEL_BINARY     := ${KERNEL_DIR}/build/kernel.elf

BOOTLOADER_DIR    := bootloader
BOOTLOADER_BINARY := ${BOOTLOADER_DIR}/build/bootx64.efi

USER_DIR        := user
USER_BINARY     := ${USER_DIR}/build/user.elf

BUILD_DIR         := ../build

USERLAND_ROOT     := ../userland_root
INITRD            := ${BUILD_DIR}/initrd.img
DISK_IMG          := ${BUILD_DIR}/kernel.img
DISK_IMG_SIZE     := 33792

QEMU_FLAGS :=                                                \
	-no-reboot -no-shutdown                                    \
	-bios OVMF.fd                                              \
	-drive if=none,id=uas-disk1,file=${DISK_IMG},format=raw    \
	-device usb-storage,drive=uas-disk1                        \
	-drive file=${BUILD_DIR}/fat32test.img,format=raw,if=ide,index=0,media=disk \
	-serial stdio                                              \
	-usb                                                       \
	-net none                                                  \
	-vga std

.PHONY: all clean emu initrd

all: ${DISK_IMG}

initrd: ${INITRD}

bootloader: ${BOOTLOADER_BINARY}

debug: ${DISK_IMG}
	qemu-system-x86_64    \
		${QEMU_FLAGS}       \
		-S                  \
		-gdb tcp::1234

emu: ${DISK_IMG}
	qemu-system-x86_64    \
		${QEMU_FLAGS}

kernel: ${KERNEL_BINARY}

user: ${USER_BINARY}

${DISK_IMG}: ${BUILD_DIR} ${BOOTLOADER_BINARY} ${KERNEL_BINARY} ${USER_BINARY} ${INITRD}
	# Create UEFI boot disk image in DOS format.
	dd if=/dev/zero of=${DISK_IMG} bs=1k count=${DISK_IMG_SIZE}
	mformat -i ${DISK_IMG} -F ::
	mmd -i ${DISK_IMG} ::/EFI
	mmd -i ${DISK_IMG} ::/EFI/BOOT
	# Copy the bootloader to the boot partition.
	mcopy -i ${DISK_IMG} ${BOOTLOADER_BINARY} ::/efi/boot/bootx64.efi
	mcopy -i ${DISK_IMG} ${KERNEL_BINARY} ::/kernel.elf
	mcopy -i ${DISK_IMG} ${USER_BINARY} ::/user.elf
	# Copy the initial ramdisk (userland root filesystem image).
	mcopy -i ${DISK_IMG} ${INITRD} ::/initrd.img


${BOOTLOADER_BINARY}:
	make -C ${BOOTLOADER_DIR}

${BUILD_DIR}:
	mkdir -p ${BUILD_DIR}

${KERNEL_BINARY}:
	make -C ${KERNEL_DIR}

${USER_BINARY}:
	make -C ${USER_DIR}


${INITRD}: ${USER_BINARY} | ${BUILD_DIR}
	# Install user binaries into userland root before packing.
	cp -f ${USER_BINARY} ${USERLAND_ROOT}/bin/user.elf
	# Build initrd as a cpio newc archive of the userland_root tree.
	cd ${USERLAND_ROOT} && find . -not -name '.gitkeep' | cpio -o -H newc > ../build/initrd.img

clean:
	make clean -C ${BOOTLOADER_DIR}
	make clean -C ${KERNEL_DIR}
	make clean -C ${USER_DIR}
	rm -f ${DISK_IMG}
	rm -f ${INITRD}
	rm -f ${USERLAND_ROOT}/bin/user.elf
	@# Preserve fat32test.img across cleans
	@if [ -f ${BUILD_DIR}/fat32test.img ]; then mv ${BUILD_DIR}/fat32test.img /tmp/_fat32test.img.bak; fi
	rm -rf ${BUILD_DIR}
	@if [ -f /tmp/_fat32test.img.bak ]; then mkdir -p ${BUILD_DIR} && mv /tmp/_fat32test.img.bak ${BUILD_DIR}/fat32test.img; fi
	

