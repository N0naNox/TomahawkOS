.POSIX:
.DELETE_ON_ERROR:
MAKEFLAGS += --warn-undefined-variables
MAKEFLAGS += --no-builtin-rules

# -------- Tools --------
CC      := x86_64-elf-gcc
LD      := x86_64-elf-ld
OBJCOPY := x86_64-elf-objcopy

# -------- Paths --------
SRC_DIR   := src
BUILD_DIR := build

C_SOURCES := \
	$(SRC_DIR)/syscall.c \
	$(SRC_DIR)/string.c \
	$(SRC_DIR)/socket.c \
	$(SRC_DIR)/stdio.c \
	$(SRC_DIR)/user_main.c

OBJECTS := $(C_SOURCES:$(SRC_DIR)/%.c=$(BUILD_DIR)/%.o)

TARGET := $(BUILD_DIR)/user.elf

# -------- Flags --------
CFLAGS := \
	-ffreestanding \
	-fno-stack-protector \
	-fno-pic \
	-fno-pie \
	-mno-red-zone \
	-Wall -Wextra \
	-I$(SRC_DIR)/include

LDFLAGS := \
	-T linker.ld \
	-nostdlib

# -------- Rules --------
.PHONY: all clean

all: $(TARGET)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGET): $(OBJECTS)
	$(LD) $(LDFLAGS) -o $@ $(OBJECTS)

clean:
	rm -rf $(BUILD_DIR)
