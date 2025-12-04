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

SRC_DIR := src

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
NASM_SOURCES := ${SRC_DIR}/hal_port_io.asm ${SRC_DIR}/idt_asm.asm

AS_OBJECTS  := ${AS_SOURCES:.S=.as.o}
NASM_OBJECTS := ${NASM_SOURCES:.asm=.o}
C_OBJECTS   := ${C_SOURCES:.c=.o}

C_SOURCES  :=              \
	${SRC_DIR}/idt.c         \
	${SRC_DIR}/hal_port_io.c \
	${SRC_DIR}/kernel.c      \
	${SRC_DIR}/string.c      \
	${SRC_DIR}/uart.c

OBJECTS := ${AS_OBJECTS} ${NASM_OBJECTS} ${C_OBJECTS}


BUILD_DIR  := build
BINARY     := ${BUILD_DIR}/kernel.elf


.PHONY: all clean

all: ${BINARY}

${BINARY}: ${BUILD_DIR} ${OBJECTS}
	${CC} -T ${SRC_DIR}/kernel.ld -g3 -o ${BINARY} ${LDFLAGS} ${OBJECTS} ${LIBS}

%.as.o: %.S
	${CC} ${INCLUDE_FLAG} -g -c $< -o $@ ${CFLAGS}


%.o: %.c
	${CC} ${INCLUDE_FLAG} -g -c $< -o $@ ${CFLAGS}

%.o: %.asm
	nasm -f elf64 $< -o $@

${BUILD_DIR}:
	mkdir -p ${BUILD_DIR}

clean:
	rm -f ${OBJECTS}
	rm -f ${BINARY}
