#!/bin/bash
#
# create_fat32_test.sh — Generate a FAT32 disk image for TomahawkOS testing.
#
# Creates a 64 MB FAT32 image with sample files and directories
# that the FAT32 driver can read/write during demos.
# Requires sudo for loop mounting.
#

set -e

IMG="build/fat32test.img"
SIZE_MB=64
MOUNT_DIR=$(mktemp -d)

echo "[FAT32 TEST] Creating ${SIZE_MB}MB FAT32 disk image: ${IMG}"

# Ensure build directory exists
mkdir -p build

# Create the raw image file
dd if=/dev/zero of="${IMG}" bs=1M count=${SIZE_MB} status=none

# Format as FAT32
#   -F 32    : Force FAT32
#   -S 512   : 512 bytes/sector
#   -s 8     : 8 sectors/cluster (4KB clusters)
#   -n TOMAHAWK : Volume label
mkfs.fat -F 32 -S 512 -s 8 -n "TOMAHAWK" "${IMG}"

echo "[FAT32 TEST] Populating test files via loop mount..."

# Mount the image and populate it
sudo mount -o loop "${IMG}" "${MOUNT_DIR}"

# Create directories
sudo mkdir -p "${MOUNT_DIR}/docs/notes"
sudo mkdir -p "${MOUNT_DIR}/src"
sudo mkdir -p "${MOUNT_DIR}/empty_dir"
sudo mkdir -p "${MOUNT_DIR}/etc"
sudo mkdir -p "${MOUNT_DIR}/home"

# Seed /etc from userland_root so init.conf and other config files exist
if [ -d "userland_root/etc" ]; then
    for f in userland_root/etc/*; do
        [ -f "$f" ] && sudo cp "$f" "${MOUNT_DIR}/etc/"
    done
fi

# Create test files with known content
echo -n "Hello from TomahawkOS FAT32!" | sudo tee "${MOUNT_DIR}/hello.txt" > /dev/null
printf "This is a test file for the FAT32 driver.\nIt has multiple lines.\nLine 3 here." | sudo tee "${MOUNT_DIR}/docs/readme.txt" > /dev/null
echo -n "Short" | sudo tee "${MOUNT_DIR}/docs/notes/short.txt" > /dev/null

# Create a file with exactly 512 bytes (one sector)
python3 -c "import sys; sys.stdout.buffer.write(b'A' * 512)" | sudo tee "${MOUNT_DIR}/sector.bin" > /dev/null

# Create a file spanning multiple clusters (4KB)
python3 -c "import sys; sys.stdout.buffer.write(bytes(range(256)) * 16)" | sudo tee "${MOUNT_DIR}/multi.bin" > /dev/null

# Create a file in the src directory
printf "// main.c\nint main(void) { return 0; }" | sudo tee "${MOUNT_DIR}/src/main.c" > /dev/null

# Sync and unmount
sudo sync
sudo umount "${MOUNT_DIR}"
rmdir "${MOUNT_DIR}"

# Ensure the image is owned by the invoking user, not root
if [ -n "${SUDO_USER}" ]; then
    chown "${SUDO_USER}:${SUDO_USER}" "${IMG}"
fi

echo "[FAT32 TEST] Image contents created."
file "${IMG}"

echo ""
echo "[FAT32 TEST] FAT32 test image created successfully: ${IMG}"
echo "[FAT32 TEST] Size: $(du -h ${IMG} | cut -f1)"
