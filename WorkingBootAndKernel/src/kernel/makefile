#####################################################################
#  Copyright (c) 2019, AJXS.
#  This program is free software; you can redistribute it and/or modify it
#  under the terms of the GNU General Public License as published by the
#  Free Software Foundation; either version 3 of the License, or
#  (at your option) any later version.
#
#  Authors:
#     Anthony <ajxs [at] panoptic.online>
#####################################################################

.POSIX:
.DELETE_ON_ERROR:
MAKEFLAGS += --warn-undefined-variables
MAKEFLAGS += --no-builtin-rules

ARCH := x86_64
CC   := ${ARCH}-elf-gcc

BUILD_DIR := build
OBJ_DIR   := ${BUILD_DIR}
SRC_DIR   := src

INCLUDE_DIRS := ${SRC_DIR}/include
INCLUDE_FLAG := ${foreach d, ${INCLUDE_DIRS}, -I$d}

CFLAGS :=               \
	-std=gnu99            \
	-ffreestanding        \
	-fno-common           \
	-O2                   \
	-Wall                 \
	-Wextra               \
	-Wmissing-prototypes  \
	-Wstrict-prototypes

LDFLAGS :=          \
	-ffreestanding    \
	-O2               \
	-nostdlib         \
	-z max-page-size=0x1000

LIBS := -lgcc


AS_SOURCES :=

NASM_SOURCES := ${SRC_DIR}/entry.asm \
	${SRC_DIR}/hal_port_io_asm.asm \
	${SRC_DIR}/idt_asm.asm \
	${SRC_DIR}/switch.asm \
	${SRC_DIR}/syscall_entry.asm

C_SOURCES  :=              \
	${SRC_DIR}/hal_port_io.c \
	${SRC_DIR}/idt.c         \
	${SRC_DIR}/keyboard.c    \
	${SRC_DIR}/kernel.c      \
	${SRC_DIR}/uart.c        \
	${SRC_DIR}/frame_alloc.c \
	${SRC_DIR}/paging.c      \
	${SRC_DIR}/page_fault_handler.c \
	${SRC_DIR}/elf_loader.c \
	${SRC_DIR}/mm.c        \
	${SRC_DIR}/page_fault_handler_mm.c \
	${SRC_DIR}/elf_loader_mm.c     \
	${SRC_DIR}/timer.c   \
	${SRC_DIR}/proc.c   \
	${SRC_DIR}/scheduler.c   \
	${SRC_DIR}/string.c   \
	${SRC_DIR}/vga.c	\
	${SRC_DIR}/syscall_init.c \
	${SRC_DIR}/syscall.c

AS_OBJECTS   := $(patsubst ${SRC_DIR}/%.S,${OBJ_DIR}/%.as.o,${AS_SOURCES})
NASM_OBJECTS := $(patsubst ${SRC_DIR}/%.asm,${OBJ_DIR}/%.o,${NASM_SOURCES})
C_OBJECTS    := $(patsubst ${SRC_DIR}/%.c,${OBJ_DIR}/%.o,${C_SOURCES})

OBJECTS := ${AS_OBJECTS} ${NASM_OBJECTS} ${C_OBJECTS}


BINARY     := ${BUILD_DIR}/kernel.elf


.PHONY: all clean

all: ${BINARY}

${BINARY}: ${BUILD_DIR} ${OBJECTS}
	${CC} -T ${SRC_DIR}/kernel.ld -g3 -o ${BINARY} ${LDFLAGS} ${OBJECTS} ${LIBS}

${OBJ_DIR}/%.as.o: ${SRC_DIR}/%.S | ${BUILD_DIR}
	${CC} ${INCLUDE_FLAG} -g -c $< -o $@ ${CFLAGS}


${OBJ_DIR}/%.o: ${SRC_DIR}/%.c | ${BUILD_DIR}
	${CC} ${INCLUDE_FLAG} -g -c $< -o $@ ${CFLAGS}

${OBJ_DIR}/%.o: ${SRC_DIR}/%.asm | ${BUILD_DIR}
	nasm -f elf64 $< -o $@

${BUILD_DIR}:
	mkdir -p ${BUILD_DIR}

clean:
	rm -f ${OBJECTS}
	rm -f ${BINARY}
	rm -rf ${BUILD_DIR}
