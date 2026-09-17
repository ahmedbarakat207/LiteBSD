# LiteBSD

A 32-bit x86 hobby OS that boots to a real BusyBox shell. Multiboot kernel, preemptive scheduler, ~39 syscalls, a toy VFS with `/proc` and `/sys`, a 1024x768 32bpp VBE framebuffer console with `/dev/fb0`, a from-scratch libc (c-lite), upstream BusyBox 1.36.1 statically linked against that libc running `hush` with interactive line editing and tab completion in ring 3, and native userland tools (`top`, `memstat`, `neofetch`, `pcinfo`, `fbtest`).

No, it's not BSD. The name is aspirational.

## Boot it

You need `i686-elf-gcc`, `nasm`, `qemu-system-i386`, and `xorriso` for the ISO target. Syslinux 6.03 binaries are fetched automatically on first `make iso` (no install needed), or reused from `/usr/lib/ISOLINUX` + `/usr/lib/syslinux` if already present. Then:

```
git submodule update --init
make run
```

That builds the kernel, rebuilds libc + busybox if needed, compiles the native tools, packs `build/initrd.tar`, and boots QEMU with `-kernel` + `-initrd` in 1024x768 32bpp VBE graphics mode. You should land in `LiteBSD:/root#` (hush). `make run-iso` does the same thing off a Syslinux/ISOLINUX ISO instead.

Other targets: `make libc`, `make busybox`, `make initrd`, `make iso`, `make clean`. `make all` builds kernel + libc + busybox + native tools + initrd.

## Layout

```
src/boot.asm        multiboot header, entry, GDT flush, all 256 ISR stubs
src/kernel.c        kernel_main + user_init (execs /bin/sh) + shell respawn
src/gdt.c           6-entry GDT + TSS
src/idt.c           IDT, PIC remap, fault handler, COM1 logging
src/paging.c        4 GB flat mapping, PSE
src/heap.c          kernel bump allocator + free list (kmalloc/kfree)
src/sched.c         round-robin tasks, fork, wait4, exit
src/syscall.c       int 0x80 dispatch + ~39 syscalls + ELF loader
src/vfs.c           path resolution + node list + getdents + synth tree (/proc, /sys, /dev)
src/sysinfo.c       CPUID, DMI / SMBIOS reader, /proc and /sys synthesizers
src/initrd.c        ustar parser that populates the VFS at boot
src/keyboard.c      PS/2 scancode → ANSI sequences, arrows, macros, 256-byte ring buffer
src/fb.c            VESA VBE 1024x768x32bpp linear framebuffer driver
src/tty.c           framebuffer console + ANSI engine (16/256/24-bit color, altscreen)
src/time.c          PIT @100Hz
src/programs/top.c       full interactive Linux top (procps-ng layout, sort, kill, raw mode)
src/programs/memstat.c   dual-engine BSD / Linux memory statistics (UMA, /proc/meminfo, gauges)
src/programs/neofetch.c  system info + FreeBSD Beastie ASCII art
src/programs/pcinfo.c    hardware & BIOS / DMI inspector
src/programs/fbtest.c    framebuffer visual demo via /dev/fb0
linker.ld           kernel linked at 1M, ENTRY(start)
isolinux.cfg        ISOLINUX + mboot.c32: kernel as multiboot, initrd.tar as module
libc/               c-lite submodule: crt0, syscalls, malloc, stdio, dirent, float...
busybox/            upstream busybox submodule + busybox.patch
busybox.config      the actual busybox config (hush, vi, top, lineedit, ~26 applets, static)
```

## Boot sequence

1. ISOLINUX (`isolinux.bin` + `mboot.c32`, built into the ISO by `make iso` via `xorriso`) loads the kernel at 1M (`linker.ld`: `. = 1M`) and passes the multiboot info pointer in `ebx`. `boot.asm:start` does `cli`, sets `esp` to an 8K stack, installs the flat GDT, pushes `ebx`, calls `kernel_main`.
2. `kernel_main` initializes the VBE 1024x768x32bpp framebuffer, clears the screen, inits GDT → IDT → paging, then reads the first multiboot module (`initrd.tar`) and hands it to `initrd_load`, which walks the ustar archive and creates every file/dir in the VFS before any task exists.
3. PIC gets remapped (0x20/0x28) and then masked to `0xFC` — only IRQ0 (timer) and IRQ1 (keyboard) stay unmasked. PIT runs at 100Hz.
4. `create_user_task(user_init)` builds the first ring-3 task. `user_init` tries `execve("/bin/sh")`, falls back to `/bin/busybox sh -i`, and if both fail it prints and exits. Then `sti` + idle loop. From here on everything is timer-driven.

## Memory map (virtual == physical, everything identity mapped)

Paging is honest about what it is: `page_table` covers 0–4M with 4K pages, `page_dir[1..1023]` covers the rest with 4M pages, every entry is `present|rw|user` (`0x07` / `0x87`). There is no isolation. Userspace can read and write kernel memory. That's not a bug I haven't gotten to, it's the architecture — see "the deal with fork" below.

```
0x000000 – 0x0FFFFF   low mem, BIOS, multiboot info
0x0B8000              legacy VGA text buffer (mirrored/supported)
0x100000 – ...        kernel image (linked at 1M)
0x400000 – 0x13FFFFF  kernel heap (HEAP_START, 16M, heap.c)
0x8000000             USER_LOAD_ADDR — busybox and native binaries loaded here
0xE0000000 (typical)  VBE Linear Framebuffer (3072 KB @ 1024x768x32bpp, mapped to /dev/fb0)
```

Userspace heap and stacks are just `kmalloc`'d chunks of the kernel heap. Keep that in mind when the scheduler section talks about sharing.

## CPU setup

GDT has 6 entries: null, kernel code/data (`0x08`/`0x10`, DPL 0), user code/data (`0x1B`/`0x23`, DPL 3), and a TSS at `0x28` whose `esp0` is rewritten on every context switch so ring-3 → ring-0 transitions land on the current task's kernel stack.

IDT has all 256 vectors. Gate `0x80` is `0xEE` (ring-3 callable, that's the syscall gate); everything else is `0x8E`. Stubs are generated with two macros because the CPU is picky: vectors 8, 10–14, 17 and 30 push their own error code (`ISR_ERRCODE`), the rest get a dummy zero pushed (`ISR_NOERRCODE`) so the C side always sees the same layout:

```
pusha / ds / es / fs / gs / int_no / err_code   <- struct interrupt_frame
```

`isr_common_stub` calls `isr_common_handler(frame)` and then does `mov esp, eax` — the handler returns *which frame to resume*, which is the entire context-switch mechanism. `0x80` → `syscall_handler`, 32–47 → `irq_handler` (0 = timer tick + `schedule`, 1 = keyboard), 0–31 → `isr_handler` (fault path).

## Scheduler

Tasks live in a circular singly-linked `ready_queue`. Each task has a 4K kernel stack with an `interrupt_frame` carved out of the top, a user stack, a 1M userspace heap window (`heap_start/brk/end`), 16 fds, and a 256-byte `cwd`. States are `RUNNING / ZOMBIE / BLOCKED`, and `schedule()` skips zombies and blocked tasks (with a guard so it can't spin forever if everything else is dead).

Timer IRQ0 at 100Hz calls `schedule()` on every tick, so it's genuinely preemptive — syscalls run with IF clear (interrupt gate), user code runs with IF set.

The interesting bits:

- **fork is vfork.** `fork_task` copies the 4K kernel stack, gives the kid its own user-stack *copy* (with `useresp`/`ebp` shifted by the delta, and only if they actually pointed inside the old stack — shifting blindly used to corrupt heap `ebp`s), but the heap is *shared* and the parent goes `TASK_BLOCKED` until the kid execs or exits. Copying the heap was tried; it leaves stale pointers everywhere (`malloc`'s free list, `argv` strings) and leaks 1M per fork. The syscall handler runs the kid first on fork return so the parent can't touch shared state mid-flight.
- **wait4 rewind trick.** If there's nothing to reap and no `WNOHANG`, `wait4` returns `-2`, and the handler rewinds `eip -= 2` (an `int $0x80` is exactly 2 bytes: `CD 80`) and reschedules, so the parent transparently retries the syscall later. Nasty, works.
- **exec forwards argv/envp.** `sys_execve_impl` snapshots argv/envp into `kmalloc`'d buffers *before* wiping `USER_LOAD_ADDR` (the strings may live in the old image), loads ELF segments, zeroes bss tails, then builds the standard i386 stack (`argc`, `argv`, `envp`, strings — the layout `crt0` already expects) on a fresh 16K stack and sets `eip/esp/useresp`. Caps: 64 args / 64 env, 1K per string, 8K total; oversize or malformed vectors fail with `-1` and leave the old image intact. `#!` scripts are resolved in-kernel (up to 4 deep, one optional arg) and re-targeted at the interpreter — but like any fork+exec, a script faults the blocked parent afterwards (same caveat as `sleep` below). Path lookups fail silently without spewing kernel debug errors to the terminal.
- **Faults kill, they don't hang.** A ring-3 fault (`cs == 0x1B`) marks the task `ZOMBIE` exit `128+11`, wakes a blocked parent, and schedules away. A kernel-mode fault still `cli; hlt`s, because at that point something is deeply wrong and pretending otherwise helps nobody. If the dead task was the init shell (`ppid == 0`), `respawn_user_shell()` starts a fresh one so the box stays usable.
- **One shell owns the keyboard.** The old kernel debug `shell()` used to race hush for scancodes *and* interleave scheduling around fork+exec, which is part of how parents ended up corrupted after failed execs. It's still in `tty.c` but no longer started; only hush reads input now.

## Syscalls

`int $0x80`, number in `eax`, args in `ebx/ecx/edx`. Numbers 1–39:

```
1 write   2 read    3 exit    4 getpid  5 fork    6 execve  7 wait4
8 getppid 9 brk     10 mmap   11 munmap 12 pipe   13 dup    14 dup2
15 kill (signal probe 0, termination)   16 ioctl (console & fb0, see below)
17 open    18 close  19 lseek  20 stat   21 fstat  22 unlink 23 mkdir
24 chdir  25 getcwd  26 getdents (custom) 27 ftruncate 28 poll
29 uname  30 rename 31 rmdir 32 symlink 33 readlink 34 lstat
35 truncate 36 access 37 link (hardlink) 38 futimens 39 utimens
```

`getdents` doesn't follow Linux's ABI — it fills the buffer with flat `(ino:u32, reclen:u32, name:NUL)` records, and `libc/src/dirent.c` knows that layout. Validation of user pointers goes through `scheduler_user_range_valid`, which accepts the USER_LOAD region, the task's heap window, the task's stack, and anything under 1M (kernel/rodata, because everything is mapped anyway).

`brk` just moves a pointer inside the preallocated 1M window; `mmap` accepts length, fd, and offset — for `/dev/fb0` it maps the physical VBE LFB directly, for anonymous memory it's `kmalloc` wearing a trenchcoat.

## Terminal & Framebuffer

The primary console is a 1024x768x32bpp VESA VBE linear framebuffer driven by `src/fb.c` and rendered through `src/tty.c`.

- **Text Grid & Font:** Uses an 8x16 bitmap font producing a 128x48 character display with smooth software scrolling and hardware blitting.
- **`/dev/fb0` Device:** Real character device registered in the VFS. Opening `/dev/fb0` and calling `mmap(NULL, 3072*1024, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0)` gives userspace direct pointer access to the linear 32-bpp display memory. `sys_ioctl` supports `FBIOGET_VSCREENINFO` and `FBIOPUT_VSCREENINFO` for dimension querying. `fbtest` demonstrates double buffering, alpha blended boxes, animated bouncing, and screen fill benchmarks.
- **ANSI Escape Engine:** Full VT100 / xterm subset:
  - Cursor positioning: `\033[H`, `\033[f`, `\033[<row>;<col>H`, relative moves `A/B/C/D`.
  - Screen / line clearing: `\033[2J` (full clear), `\033[K` (erase to end of line), `\033[1K`, `\033[2K`.
  - Colors: Standard 16 ANSI colors, 256-color palette (`\033[38;5;<n>m` / `\033[48;5;<n>m`), and 24-bit RGB truecolor (`\033[38;2;<r>;<g>;<b>m`).
  - Attributes: Bold, dim, underline, reverse video / standout (`\033[7m`).
  - Terminal state: Save/restore cursor (`\033[s` / `\033[u`, `\0337` / `\0338`), alternate screen buffer (`\033[?1049h` enter altscreen, `\033[?1049l` restore main screen — vi exits back to your shell without leaving residual text).
- **Termios & Window Size:** Global termios handles canonical line editing vs raw non-canonical mode. `TIOCGWINSZ` returns `rows=48, cols=128, xpixel=1024, ypixel=768` so full-screen tools (`top`, `vi`) automatically adapt to the geometry.

## Keyboard & Line Editing

The PS/2 keyboard driver (`src/keyboard.c`) translates scancodes into standard ANSI escape sequences:

- **Arrow Keys:** Up (`\033[A`), Down (`\033[B`), Right (`\033[C`), Left (`\033[D`).
- **Extended Cluster:** Home (`\033[H`), End (`\033[F`), PageUp (`\033[5~`), PageDown (`\033[6~`), Insert (`\033[2~`), Delete (`\033[3~`).
- **Keypad:** Keypad Enter (`\n`), Keypad Slash (`/`), Keypad `+`, `-`, `*`.
- **Tab Key:** Generates `\t` (`0x09`), Shift-Tab generates backtab (`\033[Z`).
- **Control Macros:** `Ctrl+C` (SIGINT / line cancel), `Ctrl+D` (EOF), `Ctrl+L` (clear and redraw), `Ctrl+U` (erase line before cursor), `Ctrl+W` (erase word backward), `Ctrl+A` / `Ctrl+E` (jump line start / end), `Ctrl+K` (kill to line end).
- **Alt Macros:** `Alt+b` (word backward), `Alt+f` (word forward), `Alt+d` (word delete forward).
- **BusyBox Lineedit:** Fixed an upstream BusyBox hush quirk where `line_input_state` remained NULL without job control. In LiteBSD, hush allocates `line_input_state` on interactive entry with `TAB_COMPLETION` and `DO_HISTORY` (64 entries). Tab autocompletes binaries in `$PATH`, built-in shell applets, and filesystem paths. Up/Down arrows walk history, Left/Right arrows perform in-line cursor movement.

## Process & Memory Monitoring

LiteBSD ships with two dedicated monitoring utilities in `/bin`:

### 1. Full Linux `top` (`/bin/top`)
Replaces basic process listing with a real-time, interactive monitor matching procps-ng Linux `top`:
- **Status Header:** Uptime, 1/5/15 load averages, Tasks breakdown (`running`, `sleeping`, `stopped`, `zombie`), `%Cpu(s)` breakdown (`us`, `sy`, `ni`, `id`, `wa`, `hi`, `si`, `st`), `MiB Mem` (`total`, `free`, `used`, `buff/cache`), `MiB Swap` (`total`, `free`, `used`, `avail Mem`).
- **Inverted Process Table:** `PID USER PR NI VIRT RES SHR S %CPU %MEM TIME+ COMMAND`. States are color-coded (green `R`, cyan `S`).
- **Interactive Controls (Raw Terminal Mode):**
  - `q` / `ESC`: Clean exit, restoring terminal mode and unhiding cursor.
  - `Space`: Force immediate refresh.
  - `h` / `?`: Interactive help screen with complete command cheatsheet.
  - `k`: Interactive kill prompt (`PID to signal/kill [default X]: `, prompts for signal number).
  - `r`: Renice prompt.
  - `d` / `s`: Change refresh delay interval.
  - `M`: Sort descending by memory usage (`%MEM`).
  - `P` / `C`: Sort descending by CPU usage (`%CPU`).
  - `T`: Sort descending by cumulative execution time (`TIME+`).
  - `N`: Sort numerically by PID.
  - `R`: Toggle normal / inverted sort order.
  - `c`: Toggle process basename vs full command line arguments.
  - `Up` / `Down` arrows: Scroll the process table vertically.
- **Batch & Scripting:** Supports `-b` (batch mode), `-n <N>` (iteration limit), `-d <sec>` (delay), `-p <PID>` (single process monitor).
- BusyBox also has its internal `top` compiled in (`busybox top -b -n 1`).

### 2. Dual-Engine `memstat` (`/bin/memstat`)
Unifies FreeBSD VM kernel statistics with Linux `/proc/meminfo` and Debian `memstat -v`:
- **Unified Mode (default):** Colored ASCII gauge bars for Physical RAM, Kernel Wired, and User Space; side-by-side Linux `/proc/meminfo` vs FreeBSD Virtual Memory Pages; top memory consumer processes.
- **FreeBSD Mode (`-b`, `--bsd`):** Authentic FreeBSD `vmstat -m` / `vmstat -z` VM statistics (`Active`, `Inactive`, `Wire Count`, `Cache`, `Free`, `Total Real` 4096-byte pages) and FreeBSD Kernel Memory Allocator UMA zones / `malloc(9)` table (`kernel_core`, `task_struct`, `page_tables`, `vfs_nodes`, `vfs_data`, `framebuffer`, `tty_buffers`, `heap_malloc`).
- **Linux Mode (`-l`, `--linux`):** Full Linux `/proc/meminfo` breakdown (`MemTotal`, `MemFree`, `MemAvailable`, `Active`, `Cached`, `Slab`, `PageTables`, `Mapped`) plus Debian virtual memory process table.
- **Process Inspector (`-p <PID>`):** Detailed virtual memory segments (Code `.text` `r-xp`, Data/Heap `.bss` `rw-p`, Stack `rw-p`), VIRT, RES, SHR, and physical RAM share percentage.
- **Watch Mode (`-w [sec]`, `-i`):** Live monitoring with clean `q` exit.
- **Human Units (`-h`, `--human`):** Automatic kB/MB/GB scaling.

### 3. Neofetch & Hardware Inspection (`/bin/neofetch`, `/bin/pcinfo`)
- `neofetch`: Renders the colored FreeBSD Beastie daemon ASCII mascot alongside system info (OS name/version, Host DMI product/vendor, Kernel, Uptime, Shell, Resolution, Terminal, CPU model/brand/family/stepping via CPUID, Memory gauges, 16-color test card).
- `pcinfo`: Inspects DMI / SMBIOS tables and CPUID feature flags.

## VFS & Synthetic Filesystems

There is no disk driver. The filesystem is a linked list of `vfs_node`s holding full paths (`/bin/ls`, ...), populated once from the initrd. `vfs_resolve_path(cwd, path)` normalizes `.`/`..`/double slashes, every syscall resolves through the caller's `cwd`, and `getdents` enumerates direct children by prefix matching (root is special-cased since every path starts with `/`). Symlinks (`S_IFLNK`), `rename`, `rmdir`, and hardlinks (shared COW blobs) are supported.

Synthetic nodes are generated dynamically in-kernel without requiring mount tables:
- **`/dev` tree:**
  - `/dev/null`: Discards writes, reads EOF.
  - `/dev/zero`: Discards writes, reads NULs.
  - `/dev/tty`, `/dev/console`: Console character devices.
  - `/dev/fb0`: Linear framebuffer character device with `mmap` support.
- **`/proc` tree:**
  - `/proc/meminfo`: Real RAM stats (`MemTotal`, `MemFree`, `MemAvailable`, `Cached`, `Active`, `Slab`, `Mapped`, `PageTables`).
  - `/proc/stat`: Aggregate CPU ticks (`cpu user nice sys idle ...`).
  - `/proc/loadavg`: Honored load averages and runnable task counts.
  - `/proc/uptime`: Total uptime and scheduler idle ticks.
  - `/proc/version`: OS release and build identity.
  - `/proc/cpuinfo`: CPU model name, family, stepping, vendor from CPUID.
  - `/proc/<pid>/stat`: Per-process state (`R`/`S`/`Z`), PPID, utime, stime, VIRT, RSS.
  - `/proc/<pid>/cmdline`: Raw null-delimited process command line arguments.
- **`/sys` tree:**
  - `/sys/devices/virtual/dmi/id/{sys_vendor,product_name,product_version}`: DMI BIOS hardware strings.

Every node carries `atime/mtime/ctime` (sec + nsec, `stat` reads them back). The clock is seconds (+10ms nsec) since boot from the 100Hz PIT.

## initrd

`make initrd` builds a fake unix tree under `build/initrd` — `/bin`, `/sbin`, `/usr/bin`, `/etc/{passwd,group,hostname,issue,profile,os-release}`, `/dev`, `/proc`, `/sys`, `/root`, `/home`, `/var/...` — copies `busybox` and the native binaries (`top`, `memstat`, `neofetch`, `pcinfo`, `fbtest`), writes the stock config files, and tars it `--format=ustar`. At boot the kernel parses that tar in place: `0`/`\0` = file, `5` = dir, `1`/`2` = hardlink (resolved by content copy from the already-loaded target, which is why tar order matters and why the `Makefile` forces a busybox relink when `libc.a` is newer than the binary).

## Userspace

- **c-lite** (`libc/` submodule): `crt0.asm`, raw `int $0x80` wrappers, `malloc` over `brk`, stdio, string, `dirent` speaking the custom getdents layout, `%f` floating-point formatting in `vsnprintf`, `div`/`ldiv`/`atof`, `mmap` with file descriptor passing, plus compat shims. Native binaries and BusyBox link against it statically (`-nostdlib`, `-Ttext,0x8000000`).
- **BusyBox 1.36.1** with an expanded config: `hush` (`SH_IS_HUSH`, `BASH_IS_HUSH`, standalone + nofork), line editing (`FEATURE_EDITING`, `TAB_COMPLETION`, `DO_HISTORY`), `vi` (minimal: colon commands on, no search/yank/signals/resize; altscreen support), `top`, and applets `cat echo ls mkdir pwd clear kill sleep test true false printf bash vi uname cp mv rm rmdir ln touch readlink realpath truncate stat free ps uptime hostname fbset`. Shell globbing (`*?[]`, dotfile rules, `dir/*`) is a real libc `glob()` over `opendir`/`readdir`/`fnmatch`.

## Debugging

Faults print one line to VGA/framebuffer *and* a detailed line to COM1 (`0x3F8`, 38400 8N1):

```
[SERIAL] CPU Exception 6 pid=1 err=0x00000000 eip=0x00000007 cs=0x0000001b
  ebp=... esp=... useresp=... ustack: 8 words ... ubase=... utop=...
  heap=start-brk-end [cr2=... on #PF]
```

Run QEMU with `-serial file:serial.log` (or `-serial stdio`) and drive it headless via the monitor (`-monitor stdio`, `sendkey`, `screendump`). That's how the fork-corruption bugs got caught — `eip=0x7` with `ebp` sitting in the heap is unmistakable once you see the register dump next to the heap range.

## Honest limitations

- No memory isolation at all. Every task shares USER code/data/bss and the kernel heap. `fork` without an MMU can't give the kid private globals at the same virtual addresses, so hush's fork path (which assumes copy semantics) pollutes shared state. The vfork discipline + fault containment keeps the box alive, but a failed exec still kills that shell instance and respawns a fresh one — you'll lose `cwd`.
- `exec` forwards `argv`/`envp`, but fork+exec of an external binary still shares USER text/data/bss with the blocked parent, so commands like `sleep` can fault the shell and trigger a respawn (NOFORK applets, including `vi`, are unaffected). Pipes/dup work at the fd level; job control doesn't exist.
- Exiting the init shell (`exit`, or quitting `vi` and tripping hush's fd-restore grumble — `can't duplicate file descriptor`, file is saved first) respawns a fresh shell instead of faulting: `sys_exit` on a `ppid == 0` task brings up `user_init` again, same as the fault path. You'll lose `cwd`.
- `wait4`'s `-2`/rewind and the `eip -= 2` assume the syscall instruction — true today, fragile forever.
- `kmalloc` has no locking; it survives because syscalls run with IF clear, but it's one `sti` in the wrong place away from corruption.
- The user heap is a fixed 1M window per task and `wait4` never frees shared heaps — long sessions leak.

Roadmap, roughly: per-process address spaces (then real fork and deleting half the hacks above), a disk driver so VFS outlives boot, signals past SIGKILL, and growing the applet set (argv forwarding is done; private per-process globals are the next blocker for external commands).

MIT, (c) 2026 Ahmed Barakat.

