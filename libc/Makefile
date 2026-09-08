CC := i686-elf-gcc
AR := i686-elf-ar
NASM := nasm

BUILD_DIR := build
SRC_DIR := src
INC_DIR := include

CFLAGS := -std=gnu11 -ffreestanding -O2 -Wall -Wextra -m32 \
	-fno-pie -fno-stack-protector -fno-builtin -I $(INC_DIR)

C_SRCS := $(SRC_DIR)/syscall.c \
          $(SRC_DIR)/string.c \
          $(SRC_DIR)/stdlib.c \
          $(SRC_DIR)/stdio.c \
          $(SRC_DIR)/errno.c

C_OBJS := $(patsubst $(SRC_DIR)/%.c, $(BUILD_DIR)/%.o, $(C_SRCS))
CRT0_OBJ := $(BUILD_DIR)/crt0.o
LIBC_A := $(BUILD_DIR)/libc.a

.PHONY: all clean test

all: $(LIBC_A) $(CRT0_OBJ)

$(BUILD_DIR):
	mkdir -p $@

$(CRT0_OBJ): $(SRC_DIR)/crt0.asm | $(BUILD_DIR)
	$(NASM) -f elf32 $< -o $@

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(LIBC_A): $(C_OBJS) | $(BUILD_DIR)
	$(AR) rcs $@ $^

test: all
	$(CC) $(CFLAGS) -nostdlib $(CRT0_OBJ) main.c $(LIBC_A) -o $(BUILD_DIR)/test_prog -Wl,-Ttext,0x8000000

clean:
	rm -rf $(BUILD_DIR)
