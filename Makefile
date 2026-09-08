TARGET := kernel
BUILD_DIR := build
ISO_DIR := $(BUILD_DIR)/iso

CC := i686-elf-gcc
LD := i686-elf-ld
NASM := nasm
GRUB_MKRESCUE ?= $(shell command -v grub-mkrescue 2>/dev/null || command -v i686-elf-grub-mkrescue 2>/dev/null)

CFLAGS := -std=gnu11 -ffreestanding -O2 -Wall -Wextra -m32 \
	-fno-pie -fno-stack-protector -fno-builtin
LDFLAGS := -m elf_i386 -T linker.ld

.PHONY: all clean iso

all: $(BUILD_DIR)/$(TARGET)

$(BUILD_DIR):
	mkdir -p $@

$(BUILD_DIR)/boot.o: src/boot.asm | $(BUILD_DIR)
	$(NASM) -f elf32 $< -o $@

$(BUILD_DIR)/kernel.o: src/kernel.c src/include/tty.h src/include/paging.h src/tty.c src/paging.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/tty.o: src/tty.c src/include/tty.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/paging.o: src/paging.c src/include/paging.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/gdt.o: src/gdt.c src/include/gdt.h src/include/tty.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/idt.o: src/idt.c src/include/idt.h src/include/tty.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/keyboard.o: src/keyboard.c src/include/keyboard.h src/include/tty.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/heap.o: src/heap.c src/include/heap.h src/include/tty.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/time.o: src/time.c src/include/time.h src/include/idt.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/vfs.o: src/vfs.c src/include/vfs.h src/include/heap.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/sched.o: src/sched.c src/include/sched.h src/include/tty.h src/include/idt.h src/include/heap.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/syscall.o: src/syscall.c src/include/syscall.h src/include/tty.h src/include/keyboard.h src/include/sched.h src/include/idt.h src/include/vfs.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/$(TARGET): $(BUILD_DIR)/boot.o $(BUILD_DIR)/kernel.o $(BUILD_DIR)/tty.o $(BUILD_DIR)/paging.o $(BUILD_DIR)/gdt.o $(BUILD_DIR)/idt.o $(BUILD_DIR)/keyboard.o $(BUILD_DIR)/heap.o $(BUILD_DIR)/time.o $(BUILD_DIR)/vfs.o $(BUILD_DIR)/sched.o $(BUILD_DIR)/syscall.o linker.ld
	$(LD) $(LDFLAGS) -o $@ $(BUILD_DIR)/boot.o $(BUILD_DIR)/kernel.o $(BUILD_DIR)/tty.o $(BUILD_DIR)/paging.o $(BUILD_DIR)/gdt.o $(BUILD_DIR)/idt.o $(BUILD_DIR)/keyboard.o $(BUILD_DIR)/heap.o $(BUILD_DIR)/time.o $(BUILD_DIR)/vfs.o $(BUILD_DIR)/sched.o $(BUILD_DIR)/syscall.o

iso: $(BUILD_DIR)/$(TARGET)
	mkdir -p $(ISO_DIR)/boot/grub
	cp $(BUILD_DIR)/$(TARGET) $(ISO_DIR)/boot/kernel
	cp grub.cfg $(ISO_DIR)/boot/grub/grub.cfg
	$(GRUB_MKRESCUE) -o $(BUILD_DIR)/$(TARGET).iso $(ISO_DIR)

clean:
	rm -rf $(BUILD_DIR)