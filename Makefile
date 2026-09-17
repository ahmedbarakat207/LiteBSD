TARGET := kernel
BUILD_DIR := build
ISO_DIR := $(BUILD_DIR)/iso

CC := i686-elf-gcc
LD := i686-elf-ld
NASM := nasm
XORRISO ?= xorriso

SYSLINUX_VERSION := 6.03
SYSLINUX_TARBALL := $(BUILD_DIR)/syslinux-$(SYSLINUX_VERSION).tar.gz
SYSLINUX_URL := https://mirrors.edge.kernel.org/pub/linux/utils/boot/syslinux/syslinux-$(SYSLINUX_VERSION).tar.gz
SYSLINUX_SRC := $(BUILD_DIR)/syslinux-$(SYSLINUX_VERSION)

CFLAGS := -std=gnu11 -ffreestanding -O2 -Wall -Wextra -m32 \
	-fno-pie -fno-stack-protector -fno-builtin
LDFLAGS := -m elf_i386 -T linker.ld

# userspace program flags (link against libc)
UCFLAGS  := -std=gnu11 -O2 -Wall -Wextra -m32 -fno-pie -fno-stack-protector \
	-I libc/include
ULDFLAGS := -m elf_i386 --gc-sections -Ttext=0x8000000
PROGS    := $(BUILD_DIR)/top $(BUILD_DIR)/memstat $(BUILD_DIR)/pcinfo $(BUILD_DIR)/fbtest $(BUILD_DIR)/neofetch

.PHONY: all clean iso run-iso syslinux libc busybox initrd run programs

all: $(BUILD_DIR)/$(TARGET) busybox programs initrd

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
	@if [ ! -f busybox/.config ] || ! cmp -s busybox.config busybox/.config; then \
		echo "Syncing busybox config..."; \
		cp busybox.config busybox/.config; \
	fi
	@if git -C busybox apply --check --reverse ../busybox.patch >/dev/null 2>&1; then \
		:; \
	else \
		echo "Patching busybox for LiteBSD..."; \
		git -C busybox checkout -- . 2>/dev/null; \
		git -C busybox apply ../busybox.patch || patch -p1 -d busybox < busybox.patch; \
	fi
	@if [ -f libc/build/libc.a ] && [ -f busybox/busybox ] && [ libc/build/libc.a -nt busybox/busybox ]; then \
		echo "libc changed, forcing busybox relink..."; \
		rm -f busybox/busybox; \
	fi
	$(MAKE) -C busybox -j4 ARCH=i386 CROSS_COMPILE=i686-elf-

programs: libc $(PROGS)

$(BUILD_DIR)/top.o: src/programs/top.c | $(BUILD_DIR)
	$(CC) $(UCFLAGS) -c $< -o $@

$(BUILD_DIR)/memstat.o: src/programs/memstat.c | $(BUILD_DIR)
	$(CC) $(UCFLAGS) -c $< -o $@

$(BUILD_DIR)/pcinfo.o: src/programs/pcinfo.c | $(BUILD_DIR)
	$(CC) $(UCFLAGS) -c $< -o $@

$(BUILD_DIR)/top: $(BUILD_DIR)/top.o
	$(LD) $(ULDFLAGS) libc/build/crt0.o $< libc/build/libc.a -o $@

$(BUILD_DIR)/memstat: $(BUILD_DIR)/memstat.o
	$(LD) $(ULDFLAGS) libc/build/crt0.o $< libc/build/libc.a -o $@

$(BUILD_DIR)/pcinfo: $(BUILD_DIR)/pcinfo.o
	$(LD) $(ULDFLAGS) libc/build/crt0.o $< libc/build/libc.a -o $@

$(BUILD_DIR)/fbtest.o: src/programs/fbtest.c | $(BUILD_DIR)
	$(CC) $(UCFLAGS) -c $< -o $@

$(BUILD_DIR)/fbtest: $(BUILD_DIR)/fbtest.o
	$(LD) $(ULDFLAGS) libc/build/crt0.o $< libc/build/libc.a -o $@

$(BUILD_DIR)/neofetch.o: src/programs/neofetch.c | $(BUILD_DIR)
	$(CC) $(UCFLAGS) -c $< -o $@

$(BUILD_DIR)/neofetch: $(BUILD_DIR)/neofetch.o
	$(LD) $(ULDFLAGS) libc/build/crt0.o $< libc/build/libc.a -o $@

initrd: busybox programs | $(BUILD_DIR)
	rm -rf $(BUILD_DIR)/initrd
	mkdir -p $(BUILD_DIR)/initrd/bin $(BUILD_DIR)/initrd/sbin $(BUILD_DIR)/initrd/etc
	mkdir -p $(BUILD_DIR)/initrd/usr/bin $(BUILD_DIR)/initrd/usr/sbin $(BUILD_DIR)/initrd/usr/lib
	mkdir -p $(BUILD_DIR)/initrd/dev $(BUILD_DIR)/initrd/proc $(BUILD_DIR)/initrd/sys $(BUILD_DIR)/initrd/tmp
	mkdir -p $(BUILD_DIR)/initrd/root $(BUILD_DIR)/initrd/home $(BUILD_DIR)/initrd/var/log $(BUILD_DIR)/initrd/var/run $(BUILD_DIR)/initrd/var/tmp
	chmod 1777 $(BUILD_DIR)/initrd/tmp
	cp busybox/busybox $(BUILD_DIR)/initrd/bin/busybox
	ln $(BUILD_DIR)/initrd/bin/busybox $(BUILD_DIR)/initrd/bin/sh 2>/dev/null || cp busybox/busybox $(BUILD_DIR)/initrd/bin/sh
	for applet in bash ls cat echo pwd clear mkdir rmdir kill sleep test true false printf vi uname cp mv rm ln touch readlink realpath truncate stat free ps uptime hostname fbset; do \
		ln $(BUILD_DIR)/initrd/bin/busybox $(BUILD_DIR)/initrd/bin/$$applet 2>/dev/null || cp $(BUILD_DIR)/initrd/bin/busybox $(BUILD_DIR)/initrd/bin/$$applet; \
		ln $(BUILD_DIR)/initrd/bin/busybox $(BUILD_DIR)/initrd/usr/bin/$$applet 2>/dev/null || true; \
	done
	ln $(BUILD_DIR)/initrd/bin/busybox $(BUILD_DIR)/initrd/sbin/busybox 2>/dev/null || true
	# native programs: top, memstat, pcinfo, fbtest, neofetch
	cp $(BUILD_DIR)/top      $(BUILD_DIR)/initrd/bin/top
	cp $(BUILD_DIR)/memstat  $(BUILD_DIR)/initrd/bin/memstat
	cp $(BUILD_DIR)/pcinfo   $(BUILD_DIR)/initrd/bin/pcinfo
	cp $(BUILD_DIR)/fbtest   $(BUILD_DIR)/initrd/bin/fbtest
	cp $(BUILD_DIR)/neofetch $(BUILD_DIR)/initrd/bin/neofetch
	echo "root:x:0:0:root:/root:/bin/sh" > $(BUILD_DIR)/initrd/etc/passwd
	echo "daemon:x:1:1:daemon:/usr/sbin:/bin/sh" >> $(BUILD_DIR)/initrd/etc/passwd
	echo "nobody:x:65534:65534:nobody:/nonexistent:/bin/false" >> $(BUILD_DIR)/initrd/etc/passwd
	echo "root:x:0:" > $(BUILD_DIR)/initrd/etc/group
	echo "daemon:x:1:" >> $(BUILD_DIR)/initrd/etc/group
	echo "tty:x:5:" >> $(BUILD_DIR)/initrd/etc/group
	echo "users:x:100:" >> $(BUILD_DIR)/initrd/etc/group
	echo "litebsd" > $(BUILD_DIR)/initrd/etc/hostname
	printf "NAME=LiteBSD\nID=litebsd\nVERSION=2.0\nVERSION_ID=2.0\nPRETTY_NAME=\"LiteBSD 2.0 (i386)\"\nHOME_URL=\"https://github.com/ahmedbarakat207/LiteBSD\"\n" > $(BUILD_DIR)/initrd/etc/os-release
	echo "Welcome to LiteBSD (i386)" > $(BUILD_DIR)/initrd/etc/issue
	printf "export PATH=/bin:/sbin:/usr/bin:/usr/sbin\nexport HOME=/root\nexport USER=root\nexport PS1='LiteBSD:\\\\w# '\nalias ll='ls -la'\nalias l='ls'\n" > $(BUILD_DIR)/initrd/etc/profile
	printf "Welcome to LiteBSD!\nKernel: LiteBSD i386 microkernel\nUserspace: Upstream BusyBox 1.36.1\nC Library: c-lite micro-libc\n" > $(BUILD_DIR)/initrd/readme.txt
	echo "Root home directory." > $(BUILD_DIR)/initrd/root/readme.txt
	touch $(BUILD_DIR)/initrd/dev/null $(BUILD_DIR)/initrd/dev/zero $(BUILD_DIR)/initrd/dev/tty $(BUILD_DIR)/initrd/dev/console $(BUILD_DIR)/initrd/dev/fb0
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

$(BUILD_DIR)/sysinfo.o: src/sysinfo.c src/include/sysinfo.h src/include/heap.h src/include/time.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/fb.o: src/fb.c src/include/fb.h src/include/font.h src/include/tty.h src/include/multiboot.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/$(TARGET): $(BUILD_DIR)/boot.o $(BUILD_DIR)/kernel.o $(BUILD_DIR)/fb.o $(BUILD_DIR)/tty.o $(BUILD_DIR)/paging.o $(BUILD_DIR)/gdt.o $(BUILD_DIR)/idt.o $(BUILD_DIR)/keyboard.o $(BUILD_DIR)/heap.o $(BUILD_DIR)/time.o $(BUILD_DIR)/vfs.o $(BUILD_DIR)/sched.o $(BUILD_DIR)/syscall.o $(BUILD_DIR)/initrd.o $(BUILD_DIR)/sysinfo.o linker.ld
	$(LD) $(LDFLAGS) -o $@ $(BUILD_DIR)/boot.o $(BUILD_DIR)/kernel.o $(BUILD_DIR)/fb.o $(BUILD_DIR)/tty.o $(BUILD_DIR)/paging.o $(BUILD_DIR)/gdt.o $(BUILD_DIR)/idt.o $(BUILD_DIR)/keyboard.o $(BUILD_DIR)/heap.o $(BUILD_DIR)/time.o $(BUILD_DIR)/vfs.o $(BUILD_DIR)/sched.o $(BUILD_DIR)/syscall.o $(BUILD_DIR)/initrd.o $(BUILD_DIR)/sysinfo.o


syslinux: $(SYSLINUX_SRC)/bios/core/isolinux.bin

$(SYSLINUX_SRC)/bios/core/isolinux.bin:
	mkdir -p $(BUILD_DIR)
	if [ ! -f $(SYSLINUX_TARBALL) ]; then \
		echo "Fetching syslinux $(SYSLINUX_VERSION)..."; \
		curl -sL -o $(SYSLINUX_TARBALL) $(SYSLINUX_URL); \
	fi
	tar -xzf $(SYSLINUX_TARBALL) -C $(BUILD_DIR)

iso: $(BUILD_DIR)/$(TARGET) initrd
	rm -rf $(ISO_DIR)
	mkdir -p $(ISO_DIR)/boot $(ISO_DIR)/isolinux
	cp $(BUILD_DIR)/$(TARGET) $(ISO_DIR)/boot/kernel
	cp $(BUILD_DIR)/initrd.tar $(ISO_DIR)/boot/initrd.tar
	cp isolinux.cfg $(ISO_DIR)/isolinux/isolinux.cfg
	ISOLINUX_BIN=""; MBOOT=""; LDLINUX=""; LIBCOM32=""; LIBUTIL=""; ISOHDPFX=""; \
	for f in /usr/lib/ISOLINUX/isolinux.bin /usr/share/syslinux/isolinux.bin; do \
		if [ -f "$$f" ]; then ISOLINUX_BIN="$$f"; break; fi; \
	done; \
	for f in /usr/lib/syslinux/modules/bios/mboot.c32 /usr/share/syslinux/mboot.c32; do \
		if [ -f "$$f" ]; then MBOOT="$$f"; break; fi; \
	done; \
	for f in /usr/lib/syslinux/modules/bios/ldlinux.c32 /usr/share/syslinux/ldlinux.c32; do \
		if [ -f "$$f" ]; then LDLINUX="$$f"; break; fi; \
	done; \
	for f in /usr/lib/syslinux/modules/bios/libcom32.c32 /usr/share/syslinux/libcom32.c32; do \
		if [ -f "$$f" ]; then LIBCOM32="$$f"; break; fi; \
	done; \
	for f in /usr/lib/syslinux/modules/bios/libutil.c32 /usr/share/syslinux/libutil.c32; do \
		if [ -f "$$f" ]; then LIBUTIL="$$f"; break; fi; \
	done; \
	for f in /usr/lib/ISOLINUX/isohdpfx.bin /usr/lib/syslinux/mbr/isohdpfx.bin /usr/share/syslinux/isohdpfx.bin; do \
		if [ -f "$$f" ]; then ISOHDPFX="$$f"; break; fi; \
	done; \
	if [ -z "$$ISOLINUX_BIN" ] || [ -z "$$MBOOT" ] || [ -z "$$LDLINUX" ]; then \
		echo "Syslinux not found on host, using pinned syslinux $(SYSLINUX_VERSION)..."; \
		$(MAKE) $(SYSLINUX_SRC)/bios/core/isolinux.bin; \
		ISOLINUX_BIN="$(SYSLINUX_SRC)/bios/core/isolinux.bin"; \
		MBOOT="$(SYSLINUX_SRC)/bios/com32/mboot/mboot.c32"; \
		LDLINUX="$(SYSLINUX_SRC)/bios/com32/elflink/ldlinux/ldlinux.c32"; \
		LIBCOM32="$(SYSLINUX_SRC)/bios/com32/lib/libcom32.c32"; \
		LIBUTIL="$(SYSLINUX_SRC)/bios/com32/libutil/libutil.c32"; \
		ISOHDPFX="$(SYSLINUX_SRC)/bios/mbr/isohdpfx.bin"; \
	fi; \
	cp "$$ISOLINUX_BIN" $(ISO_DIR)/isolinux/isolinux.bin; \
	cp "$$MBOOT" $(ISO_DIR)/isolinux/mboot.c32; \
	cp "$$LDLINUX" $(ISO_DIR)/isolinux/ldlinux.c32; \
	if [ -n "$$LIBCOM32" ] && [ -f "$$LIBCOM32" ]; then cp "$$LIBCOM32" $(ISO_DIR)/isolinux/libcom32.c32; fi; \
	if [ -n "$$LIBUTIL" ] && [ -f "$$LIBUTIL" ]; then cp "$$LIBUTIL" $(ISO_DIR)/isolinux/libutil.c32; fi; \
	if ! command -v $(XORRISO) >/dev/null 2>&1; then \
		echo "error: $(XORRISO) not found (brew install xorriso / apt install xorriso)"; exit 1; \
	fi; \
	HYBRID=""; \
	if [ -n "$$ISOHDPFX" ] && [ -f "$$ISOHDPFX" ]; then HYBRID="-isohybrid-mbr $$ISOHDPFX"; fi; \
	$(XORRISO) -as mkisofs -o $(BUILD_DIR)/$(TARGET).iso \
		-iso-level 3 -J -R -l \
		-b isolinux/isolinux.bin -c isolinux/boot.cat \
		-no-emul-boot -boot-load-size 4 -boot-info-table \
		$$HYBRID \
		$(ISO_DIR)

run-iso: iso
	qemu-system-i386 -m 512M -cdrom $(BUILD_DIR)/$(TARGET).iso

clean:
	rm -rf $(BUILD_DIR)