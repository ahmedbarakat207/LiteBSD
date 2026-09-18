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
	-fno-pie -fno-stack-protector -fno-builtin \
	-I src -I src/include -I src/drivers -I src/drivers/include
LDFLAGS := -m elf_i386 -T linker.ld

UCFLAGS  := -std=gnu11 -O2 -Wall -Wextra -m32 -fno-pie -fno-stack-protector \
	-I libc/include
ULDFLAGS := -m elf_i386 --gc-sections -Ttext=0x8000000
PROGS    := $(BUILD_DIR)/memstat $(BUILD_DIR)/pcinfo $(BUILD_DIR)/fbtest $(BUILD_DIR)/forktest

.PHONY: all clean iso run-iso syslinux libc busybox initrd run programs ifconfig curl

all: $(BUILD_DIR)/$(TARGET) busybox programs $(BUILD_DIR)/ifconfig $(BUILD_DIR)/curl initrd

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

$(BUILD_DIR)/memstat.o: src/programs/memstat.c | $(BUILD_DIR)
	$(CC) $(UCFLAGS) -c $< -o $@

$(BUILD_DIR)/pcinfo.o: src/programs/pcinfo.c | $(BUILD_DIR)
	$(CC) $(UCFLAGS) -c $< -o $@

$(BUILD_DIR)/memstat: $(BUILD_DIR)/memstat.o libc/build/libc.a
	$(LD) $(ULDFLAGS) libc/build/crt0.o $< libc/build/libc.a -o $@

$(BUILD_DIR)/pcinfo: $(BUILD_DIR)/pcinfo.o libc/build/libc.a
	$(LD) $(ULDFLAGS) libc/build/crt0.o $< libc/build/libc.a -o $@

$(BUILD_DIR)/fbtest.o: src/programs/fbtest.c | $(BUILD_DIR)
	$(CC) $(UCFLAGS) -c $< -o $@

$(BUILD_DIR)/fbtest: $(BUILD_DIR)/fbtest.o libc/build/libc.a
	$(LD) $(ULDFLAGS) libc/build/crt0.o $< libc/build/libc.a -o $@

$(BUILD_DIR)/forktest.o: src/programs/forktest.c | $(BUILD_DIR)
	$(CC) $(UCFLAGS) -c $< -o $@

$(BUILD_DIR)/forktest: $(BUILD_DIR)/forktest.o libc/build/libc.a
	$(LD) $(ULDFLAGS) libc/build/crt0.o $< libc/build/libc.a -o $@

ifconfig: $(BUILD_DIR)/ifconfig

$(BUILD_DIR)/ifconfig: libc | $(BUILD_DIR)
	@if [ ! -f src/programs/net-tools/Makefile ]; then \
		echo "Initializing net-tools submodule..."; \
		git submodule update --init src/programs/net-tools; \
	fi
	$(MAKE) -C src/programs/net-tools/lib CC=$(CC) AR=i686-elf-ar \
		CFLAGS="-std=gnu11 -O2 -m32 -fno-pie -fno-stack-protector -ffunction-sections -fdata-sections -I$(CURDIR)/libc/include -I$(CURDIR)/src/programs/net-tools/include -I. -I.. -D_GNU_SOURCE"
	$(CC) -std=gnu11 -O2 -m32 -fno-pie -fno-stack-protector -ffunction-sections -fdata-sections \
		-I$(CURDIR)/libc/include -I$(CURDIR)/src/programs/net-tools/include -I$(CURDIR)/src/programs/net-tools -I$(CURDIR)/src/programs/net-tools/lib -D_GNU_SOURCE \
		-c src/programs/net-tools/ifconfig.c -o $(BUILD_DIR)/ifconfig.o
	$(CC) -m32 -fno-pie -nostdlib libc/build/crt0.o $(BUILD_DIR)/ifconfig.o src/programs/net-tools/lib/libnet-tools.a libc/build/libc.a -lgcc \
		-Wl,-Ttext=0x8000000 -Wl,--gc-sections -o $@

curl: $(BUILD_DIR)/curl

$(BUILD_DIR)/curl: libc | $(BUILD_DIR)
	@if [ ! -f src/programs/curl/CMakeLists.txt ]; then \
		echo "Initializing curl submodule..."; \
		git submodule update --init src/programs/curl; \
	fi
	mkdir -p $(BUILD_DIR)/curl_build
	cd $(BUILD_DIR)/curl_build && \
	if [ ! -f Makefile ]; then \
		cmake $(CURDIR)/src/programs/curl \
			-DCMAKE_SYSTEM_NAME=Generic \
			-DCMAKE_C_COMPILER=$(CC) \
			-DCMAKE_C_FLAGS="-std=gnu11 -O2 -m32 -fno-pie -fno-stack-protector -I$(CURDIR)/libc/include" \
			-DCMAKE_EXE_LINKER_FLAGS="-nostdlib $(CURDIR)/libc/build/crt0.o $(CURDIR)/libc/build/libc.a -lgcc -Wl,-Ttext=0x8000000" \
			-DBUILD_SHARED_LIBS=OFF \
			-DBUILD_STATIC_LIBS=ON \
			-DBUILD_CURL_EXE=ON \
			-DHTTP_ONLY=ON \
			-DBUILD_TESTING=OFF \
			-DBUILD_LIBCURL_DOCS=OFF \
			-DBUILD_MISC_DOCS=OFF \
			-DCURL_DISABLE_LDAP=ON \
			-DCURL_DISABLE_LDAPS=ON \
			-DCURL_DISABLE_TELNET=ON \
			-DCURL_DISABLE_DICT=ON \
			-DCURL_DISABLE_TFTP=ON \
			-DCURL_DISABLE_POP3=ON \
			-DCURL_DISABLE_IMAP=ON \
			-DCURL_DISABLE_SMTP=ON \
			-DCURL_DISABLE_GOPHER=ON \
			-DCURL_DISABLE_MQTT=ON \
			-DCURL_DISABLE_MANUAL=ON \
			-DCURL_DISABLE_ALTSVC=ON \
			-DCURL_DISABLE_HSTS=ON \
			-DCURL_USE_LIBPSL=OFF \
			-DCURL_USE_LIBSSH2=OFF \
			-DCURL_USE_GSSAPI=OFF \
			-DENABLE_IPV6=OFF \
			-DENABLE_THREADED_RESOLVER=OFF \
			-DENABLE_UNIX_SOCKETS=OFF \
			-DCURL_HIDDEN_SYMBOLS=OFF \
			-DCURL_ENABLE_SSL=OFF; \
	fi && \
	$(MAKE) -C lib libcurl_static && \
	$(MAKE) -C src curltool
	$(CC) -std=gnu11 -O2 -m32 -fno-pie -fno-stack-protector \
		-I$(CURDIR)/libc/include -I$(CURDIR)/src/programs/curl/include -I$(CURDIR)/src/programs/curl/src -I$(CURDIR)/src/programs/curl/lib \
		-I$(CURDIR)/$(BUILD_DIR)/curl_build/lib -I$(CURDIR)/$(BUILD_DIR)/curl_build/src \
		-DCURL_STATICLIB -DHAVE_CONFIG_H \
		-c $(CURDIR)/src/programs/curl/src/tool_main.c -o $(BUILD_DIR)/curl_build/src/tool_main.o
	cd $(BUILD_DIR)/curl_build/src && \
	$(CC) -std=gnu11 -O2 -m32 -fno-pie -fno-stack-protector -nostdlib $(CURDIR)/libc/build/crt0.o \
		-Wl,-Ttext=0x8000000 -Wl,--gc-sections tool_main.o \
		-Wl,--whole-archive libcurltool.a -Wl,--no-whole-archive \
		-o $(CURDIR)/$@ ../lib/libcurl.a $(CURDIR)/libc/build/libc.a -lgcc

initrd: busybox programs $(BUILD_DIR)/ifconfig $(BUILD_DIR)/curl | $(BUILD_DIR)
	rm -rf $(BUILD_DIR)/initrd
	mkdir -p $(BUILD_DIR)/initrd/bin $(BUILD_DIR)/initrd/sbin $(BUILD_DIR)/initrd/etc
	mkdir -p $(BUILD_DIR)/initrd/usr/bin $(BUILD_DIR)/initrd/usr/sbin $(BUILD_DIR)/initrd/usr/lib
	mkdir -p $(BUILD_DIR)/initrd/dev $(BUILD_DIR)/initrd/proc $(BUILD_DIR)/initrd/sys $(BUILD_DIR)/initrd/tmp
	mkdir -p $(BUILD_DIR)/initrd/root $(BUILD_DIR)/initrd/home $(BUILD_DIR)/initrd/var/log $(BUILD_DIR)/initrd/var/run $(BUILD_DIR)/initrd/var/tmp
	chmod 1777 $(BUILD_DIR)/initrd/tmp
	cp busybox/busybox $(BUILD_DIR)/initrd/bin/busybox
	ln $(BUILD_DIR)/initrd/bin/busybox $(BUILD_DIR)/initrd/bin/sh 2>/dev/null || cp busybox/busybox $(BUILD_DIR)/initrd/bin/sh
	for applet in bash ls cat echo pwd clear mkdir rmdir kill sleep test true false printf vi uname cp mv rm ln touch readlink realpath truncate stat free ps uptime hostname fbset ping top; do \
		ln $(BUILD_DIR)/initrd/bin/busybox $(BUILD_DIR)/initrd/bin/$$applet 2>/dev/null || cp $(BUILD_DIR)/initrd/bin/busybox $(BUILD_DIR)/initrd/bin/$$applet; \
		ln $(BUILD_DIR)/initrd/bin/busybox $(BUILD_DIR)/initrd/usr/bin/$$applet 2>/dev/null || true; \
	done
	ln $(BUILD_DIR)/initrd/bin/busybox $(BUILD_DIR)/initrd/sbin/busybox 2>/dev/null || true
	# authentic programs: memstat, pcinfo, fbtest, ifconfig (net-tools), curl (curl/curl), neofetch
	cp $(BUILD_DIR)/memstat  $(BUILD_DIR)/initrd/bin/memstat
	cp $(BUILD_DIR)/forktest $(BUILD_DIR)/initrd/bin/forktest
	cp $(BUILD_DIR)/pcinfo   $(BUILD_DIR)/initrd/bin/pcinfo
	cp $(BUILD_DIR)/fbtest   $(BUILD_DIR)/initrd/bin/fbtest
	cp $(BUILD_DIR)/ifconfig $(BUILD_DIR)/initrd/bin/ifconfig
	ln $(BUILD_DIR)/initrd/bin/ifconfig $(BUILD_DIR)/initrd/sbin/ifconfig 2>/dev/null || true
	cp $(BUILD_DIR)/curl     $(BUILD_DIR)/initrd/bin/curl
	cp src/programs/neofetch/neofetch     $(BUILD_DIR)/initrd/bin/neofetch
	chmod +x $(BUILD_DIR)/initrd/bin/neofetch
	echo "nameserver 10.0.2.3" > $(BUILD_DIR)/initrd/etc/resolv.conf
	printf "127.0.0.1\tlocalhost\n10.0.2.15\tlitebsd\n" > $(BUILD_DIR)/initrd/etc/hosts
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

NIC ?= e1000

run: all
	qemu-system-i386 -m 512M -kernel $(BUILD_DIR)/$(TARGET) -initrd $(BUILD_DIR)/initrd.tar -nic model=$(NIC)

$(BUILD_DIR):
	mkdir -p $@

$(BUILD_DIR)/boot.o: src/boot.asm | $(BUILD_DIR)
	$(NASM) -f elf32 $< -o $@

$(BUILD_DIR)/kernel.o: src/kernel.c src/include/tty.h src/include/paging.h src/drivers/include/drivers.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/drivers.o: src/drivers/drivers.c src/drivers/include/drivers.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/tty.o: src/tty.c src/include/tty.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/paging.o: src/paging.c src/include/paging.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/gdt.o: src/gdt.c src/include/gdt.h src/include/tty.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/idt.o: src/idt.c src/include/idt.h src/include/tty.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/keyboard.o: src/drivers/keyboard.c src/drivers/include/keyboard.h src/include/tty.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/heap.o: src/heap.c src/include/heap.h src/include/tty.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/time.o: src/time.c src/include/time.h src/include/idt.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/vfs.o: src/vfs.c src/include/vfs.h src/include/heap.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/sched.o: src/sched.c src/include/sched.h src/include/tty.h src/include/idt.h src/include/heap.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/syscall.o: src/syscall.c src/include/syscall.h src/include/tty.h src/drivers/include/keyboard.h src/include/sched.h src/include/idt.h src/include/vfs.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/initrd.o: src/initrd.c src/include/initrd.h src/include/vfs.h src/include/tty.h src/include/heap.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/sysinfo.o: src/sysinfo.c src/include/sysinfo.h src/include/heap.h src/include/time.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/fb.o: src/drivers/fb.c src/drivers/include/fb.h src/drivers/include/font.h src/include/tty.h src/include/multiboot.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/pci.o: src/drivers/pci.c src/drivers/include/pci.h src/include/io.h src/include/tty.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/netdev.o: src/drivers/netdev.c src/drivers/include/netdev.h src/include/heap.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/e1000.o: src/drivers/e1000.c src/drivers/include/e1000.h src/drivers/include/pci.h src/drivers/include/netdev.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/ne2k.o: src/drivers/ne2k.c src/drivers/include/ne2k.h src/drivers/include/pci.h src/drivers/include/netdev.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/rtl8139.o: src/drivers/rtl8139.c src/drivers/include/rtl8139.h src/drivers/include/pci.h src/drivers/include/netdev.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/netstack.o: src/netstack.c src/include/netstack.h src/drivers/include/netdev.h src/include/socket.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/socket.o: src/socket.c src/include/socket.h src/include/netstack.h src/drivers/include/netdev.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

KERNEL_OBJS := $(BUILD_DIR)/boot.o $(BUILD_DIR)/kernel.o $(BUILD_DIR)/drivers.o $(BUILD_DIR)/fb.o $(BUILD_DIR)/tty.o \
               $(BUILD_DIR)/paging.o $(BUILD_DIR)/gdt.o $(BUILD_DIR)/idt.o $(BUILD_DIR)/keyboard.o \
               $(BUILD_DIR)/heap.o $(BUILD_DIR)/time.o $(BUILD_DIR)/vfs.o $(BUILD_DIR)/sched.o \
               $(BUILD_DIR)/syscall.o $(BUILD_DIR)/initrd.o $(BUILD_DIR)/sysinfo.o \
               $(BUILD_DIR)/pci.o $(BUILD_DIR)/netdev.o $(BUILD_DIR)/e1000.o $(BUILD_DIR)/ne2k.o \
               $(BUILD_DIR)/rtl8139.o $(BUILD_DIR)/netstack.o $(BUILD_DIR)/socket.o

$(BUILD_DIR)/$(TARGET): $(KERNEL_OBJS) linker.ld
	$(LD) $(LDFLAGS) -o $@ $(KERNEL_OBJS)


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