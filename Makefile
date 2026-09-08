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

.PHONY: all clean iso run-iso libc busybox initrd run

all: $(BUILD_DIR)/$(TARGET) busybox initrd

libc:
	@if [ ! -f libc/Makefile ]; then \
		echo "Initializing libc submodule..."; \
		git submodule update --init libc; \
	fi
	$(MAKE) -C libc

busybox: libc
	@if [ ! -f busybox/Makefile ]; then \
		echo "Initializing busybox submodule..."; \
		git submodule update --init busybox; \
	fi
	@if [ ! -f busybox/.config ]; then \
		echo "Configuring busybox..."; \
		cp busybox.config busybox/.config; \
	fi
	@if ! grep -q "APPLET_NOFORK(cat" busybox/coreutils/cat.c 2>/dev/null; then \
		echo "Patching busybox for LiteBSD..."; \
		git -C busybox apply ../busybox.patch || patch -p1 -d busybox < busybox.patch; \
	fi
	$(MAKE) -C busybox -j4 ARCH=i386 CROSS_COMPILE=i686-elf-

initrd: busybox | $(BUILD_DIR)
	rm -rf $(BUILD_DIR)/initrd
	mkdir -p $(BUILD_DIR)/initrd/bin $(BUILD_DIR)/initrd/sbin $(BUILD_DIR)/initrd/etc
	mkdir -p $(BUILD_DIR)/initrd/usr/bin $(BUILD_DIR)/initrd/usr/sbin $(BUILD_DIR)/initrd/usr/lib
	mkdir -p $(BUILD_DIR)/initrd/dev $(BUILD_DIR)/initrd/proc $(BUILD_DIR)/initrd/sys $(BUILD_DIR)/initrd/tmp
	mkdir -p $(BUILD_DIR)/initrd/root $(BUILD_DIR)/initrd/home $(BUILD_DIR)/initrd/var/log $(BUILD_DIR)/initrd/var/run $(BUILD_DIR)/initrd/var/tmp
	chmod 1777 $(BUILD_DIR)/initrd/tmp
	cp busybox/busybox $(BUILD_DIR)/initrd/bin/busybox
	cp busybox/busybox $(BUILD_DIR)/initrd/bin/sh
	for applet in bash ls cat echo pwd clear mkdir rmdir kill sleep test true false printf; do \
		ln $(BUILD_DIR)/initrd/bin/busybox $(BUILD_DIR)/initrd/bin/$$applet 2>/dev/null || cp $(BUILD_DIR)/initrd/bin/busybox $(BUILD_DIR)/initrd/bin/$$applet; \
		ln $(BUILD_DIR)/initrd/bin/busybox $(BUILD_DIR)/initrd/usr/bin/$$applet 2>/dev/null || true; \
	done
	ln $(BUILD_DIR)/initrd/bin/busybox $(BUILD_DIR)/initrd/sbin/busybox 2>/dev/null || true
	echo "root:x:0:0:root:/root:/bin/sh" > $(BUILD_DIR)/initrd/etc/passwd
	echo "daemon:x:1:1:daemon:/usr/sbin:/bin/sh" >> $(BUILD_DIR)/initrd/etc/passwd
	echo "nobody:x:65534:65534:nobody:/nonexistent:/bin/false" >> $(BUILD_DIR)/initrd/etc/passwd
	echo "root:x:0:" > $(BUILD_DIR)/initrd/etc/group
	echo "daemon:x:1:" >> $(BUILD_DIR)/initrd/etc/group
	echo "tty:x:5:" >> $(BUILD_DIR)/initrd/etc/group
	echo "users:x:100:" >> $(BUILD_DIR)/initrd/etc/group
	echo "litebsd" > $(BUILD_DIR)/initrd/etc/hostname
	echo "Welcome to LiteBSD (i386)" > $(BUILD_DIR)/initrd/etc/issue
	printf "export PATH=/bin:/sbin:/usr/bin:/usr/sbin\nexport HOME=/root\nexport USER=root\nexport PS1='LiteBSD:\\\\w# '\nalias ll='ls -la'\nalias l='ls'\n" > $(BUILD_DIR)/initrd/etc/profile
	printf "Welcome to LiteBSD!\nKernel: LiteBSD i386 microkernel\nUserspace: Upstream BusyBox 1.36.1\nC Library: c-lite micro-libc\n" > $(BUILD_DIR)/initrd/readme.txt
	echo "Root home directory." > $(BUILD_DIR)/initrd/root/readme.txt
	touch $(BUILD_DIR)/initrd/dev/null $(BUILD_DIR)/initrd/dev/zero $(BUILD_DIR)/initrd/dev/tty $(BUILD_DIR)/initrd/dev/console
	cd $(BUILD_DIR)/initrd && tar -cf ../initrd.tar --format=ustar .

run: all
	qemu-system-i386 -m 512M -kernel $(BUILD_DIR)/$(TARGET) -initrd $(BUILD_DIR)/initrd.tar

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

$(BUILD_DIR)/initrd.o: src/initrd.c src/include/initrd.h src/include/vfs.h src/include/tty.h src/include/heap.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/$(TARGET): $(BUILD_DIR)/boot.o $(BUILD_DIR)/kernel.o $(BUILD_DIR)/tty.o $(BUILD_DIR)/paging.o $(BUILD_DIR)/gdt.o $(BUILD_DIR)/idt.o $(BUILD_DIR)/keyboard.o $(BUILD_DIR)/heap.o $(BUILD_DIR)/time.o $(BUILD_DIR)/vfs.o $(BUILD_DIR)/sched.o $(BUILD_DIR)/syscall.o $(BUILD_DIR)/initrd.o linker.ld
	$(LD) $(LDFLAGS) -o $@ $(BUILD_DIR)/boot.o $(BUILD_DIR)/kernel.o $(BUILD_DIR)/tty.o $(BUILD_DIR)/paging.o $(BUILD_DIR)/gdt.o $(BUILD_DIR)/idt.o $(BUILD_DIR)/keyboard.o $(BUILD_DIR)/heap.o $(BUILD_DIR)/time.o $(BUILD_DIR)/vfs.o $(BUILD_DIR)/sched.o $(BUILD_DIR)/syscall.o $(BUILD_DIR)/initrd.o

iso: $(BUILD_DIR)/$(TARGET) initrd
	mkdir -p $(ISO_DIR)/boot/grub
	cp $(BUILD_DIR)/$(TARGET) $(ISO_DIR)/boot/kernel
	cp $(BUILD_DIR)/initrd.tar $(ISO_DIR)/boot/initrd.tar
	cp grub.cfg $(ISO_DIR)/boot/grub/grub.cfg
	$(GRUB_MKRESCUE) -o $(BUILD_DIR)/$(TARGET).iso $(ISO_DIR)

run-iso: iso
	qemu-system-i386 -m 512M -cdrom $(BUILD_DIR)/$(TARGET).iso

clean:
	rm -rf $(BUILD_DIR)