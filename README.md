# LiteBSD

A 32-bit x86 hobby OS that boots to a real BusyBox shell. Multiboot kernel, preemptive scheduler, ~26 syscalls, a toy VFS + initrd, a from-scratch libc (c-lite), and upstream BusyBox 1.36.1 statically linked against that libc running `hush` in ring 3.

No, it's not BSD. The name is aspirational.

## Boot it

You need `i686-elf-gcc`, `nasm`, `qemu-system-i386`, and `xorriso` for the ISO target. Syslinux 6.03 binaries are fetched automatically on first `make iso` (no install needed), or reused from `/usr/lib/ISOLINUX` + `/usr/lib/syslinux` if already present. Then:

```
git submodule update --init
make run
```

That builds the kernel, rebuilds libc + busybox if needed, packs `build/initrd.tar`, and boots QEMU with `-kernel` + `-initrd`. You should land in `LiteBSD:/root#` (hush). `make run-iso` does the same thing off a Syslinux/ISOLINUX ISO instead.

Other targets: `make libc`, `make busybox`, `make initrd`, `make iso`, `make clean`. `make all` is kernel + busybox + initrd.

## Layout

```
src/boot.asm      multiboot header, entry, GDT flush, all 256 ISR stubs
src/kernel.c      kernel_main + user_init (execs /bin/sh) + shell respawn
src/gdt.c         6-entry GDT + TSS
src/idt.c         IDT, PIC remap, fault handler, COM1 logging
src/paging.c      4 GB flat mapping, PSE
src/heap.c        kernel bump allocator + free list (kmalloc/kfree)
src/sched.c       round-robin tasks, fork, wait4, exit
src/syscall.c     int 0x80 dispatch + all 26 syscalls + ELF loader
src/vfs.c         path resolution + node list + getdents
src/initrd.c      ustar parser that populates the VFS at boot
src/keyboard.c    PS/2 scancode → 256-byte ring buffer
src/tty.c         VGA text driver + ANSI swallowing + kernel debug shell
src/time.c        PIT @100Hz
linker.ld         kernel linked at 1M, ENTRY(start)
isolinux.cfg      ISOLINUX + mboot.c32: kernel as multiboot, initrd.tar as module
libc/             c-lite submodule: crt0, syscalls, malloc, stdio, dirent...
busybox/          upstream busybox submodule + busybox.patch
busybox.config    the actual busybox config (hush, ~15 applets, static)
```

## Boot sequence

1. ISOLINUX (`isolinux.bin` + `mboot.c32`, built into the ISO by `make iso` via `xorriso`) loads the kernel at 1M (`linker.ld`: `. = 1M`) and passes the multiboot info pointer in `ebx`. `boot.asm:start` does `cli`, sets `esp` to an 8K stack, installs the flat GDT, pushes `ebx`, calls `kernel_main`.
2. `kernel_main` clears VGA, inits GDT → IDT → paging, then reads the first multiboot module (`initrd.tar`) and hands it to `initrd_load`, which walks the ustar archive and creates every file/dir in the VFS before any task exists.
3. PIC gets remapped (0x20/0x28) and then masked to `0xFC` — only IRQ0 (timer) and IRQ1 (keyboard) stay unmasked. PIT runs at 100Hz.
4. `create_user_task(user_init)` builds the first ring-3 task. `user_init` tries `execve("/bin/sh")`, falls back to `/bin/busybox sh -i`, and if both fail it prints and exits. Then `sti` + idle loop. From here on everything is timer-driven.

## Memory map (virtual == physical, everything identity mapped)

Paging is honest about what it is: `page_table` covers 0–4M with 4K pages, `page_dir[1..1023]` covers the rest with 4M pages, every entry is `present|rw|user` (`0x07` / `0x87`). There is no isolation. Userspace can read and write kernel memory. That's not a bug I haven't gotten to, it's the architecture — see "the deal with fork" below.

```
0x000000 – 0x0FFFFF   low mem, BIOS, multiboot info
0x0B8000              VGA text buffer
0x100000 – ...        kernel image (linked at 1M)
0x400000 – 0x13FFFFF  kernel heap (HEAP_START, 16M, heap.c)
0x8000000             USER_LOAD_ADDR — busybox is loaded here
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
- **exec drops argv.** `sys_execve_impl` loads ELF segments to `USER_LOAD_ADDR`, zeroes bss tails, makes a fresh zeroed 16K stack, sets `eip/esp/useresp`, and returns. It does *not* push `argc/argv/envp` — `crt0` sees `argc <= 0` and falls back to running `sh`. Fine for the init shell, lossy for everything else.
- **Faults kill, they don't hang.** A ring-3 fault (`cs == 0x1B`) marks the task `ZOMBIE` exit `128+11`, wakes a blocked parent, and schedules away. A kernel-mode fault still `cli; hlt`s, because at that point something is deeply wrong and pretending otherwise helps nobody. If the dead task was the init shell (`ppid == 0`), `respawn_user_shell()` starts a fresh one so the box stays usable.
- **One shell owns the keyboard.** The old kernel debug `shell()` used to race hush for scancodes *and* interleave scheduling around fork+exec, which is part of how parents ended up corrupted after failed execs. It's still in `tty.c` but no longer started; only hush reads input now.

## Syscalls

`int $0x80`, number in `eax`, args in `ebx/ecx/edx`. Numbers 1–26:

```
1 write   2 read    3 exit    4 getpid  5 fork    6 execve  7 wait4
8 getppid 9 brk     10 mmap   11 munmap 12 pipe   13 dup    14 dup2
15 kill (only SIGKILL/9 does anything)  16 ioctl (stub, -1)
17 open    18 close  19 lseek  20 stat   21 fstat  22 unlink 23 mkdir
24 chdir  25 getcwd  26 getdents (custom)
```

`getdents` doesn't follow Linux's ABI — it fills the buffer with flat `(ino:u32, reclen:u32, name:NUL)` records, and `libc/src/dirent.c` knows that layout. Validation of user pointers goes through `scheduler_user_range_valid`, which accepts the USER_LOAD region, the task's heap window, the task's stack, and anything under 1M (kernel/rodata, because everything is mapped anyway).

`brk` just moves a pointer inside the preallocated 1M window; `mmap` is `kmalloc` wearing a trenchcoat.

## VFS

There is no disk driver. The filesystem is a linked list of `vfs_node`s holding full paths (`/bin/ls`, ...), populated once from the initrd. `vfs_resolve_path(cwd, path)` normalizes `.`/`..`/double slashes, every syscall resolves through the caller's `cwd`, and `getdents` enumerates direct children by prefix matching (root is special-cased since every path starts with `/`). `refs` are bumped on open; directories are just nodes with `S_IFDIR`.

## initrd

`make initrd` builds a fake unix tree under `build/initrd` — `/bin`, `/sbin`, `/usr/bin`, `/etc/{passwd,group,hostname,issue,profile}`, `/dev`, `/proc`, `/sys`, `/root`, `/home`, `/var/...` — copies the one `busybox` binary everywhere (hardlinks where the filesystem allows, plain copies for `/bin/sh` + `/sbin/busybox`), writes the stock config files, and tars it `--format=ustar`. At boot the kernel parses that tar in place: `0`/`\0` = file, `5` = dir, `1`/`2` = hardlink (resolved by content copy from the already-loaded target, which is why tar order matters and why the `Makefile` forces a busybox relink when `libc.a` is newer than the binary).

## Userspace

- **c-lite** (`libc/` submodule): `crt0.asm`, raw `int $0x80` wrappers, `malloc` over `brk`, stdio, string, `dirent` speaking the custom getdents layout, plus compat shims. BusyBox links against it statically (`-nostdlib`, `-Ttext,0x8000000`).
- **BusyBox 1.36.1** with a small config: `hush` (`SH_IS_HUSH`, `BASH_IS_HUSH`, standalone + nofork), and applets `cat echo ls mkdir pwd clear kill sleep test true false printf bash`. `busybox.patch` flips `ls`/`cat` to `APPLET_NOFORK` so they run in-process (no fork+exec round trip through a loader that drops argv anyway), fixes a link-line quoting bug in `trylink`, and drops libm.

## Debugging

Faults print one line to VGA *and* a detailed line to COM1 (`0x3F8`, 38400 8N1):

```
[SERIAL] CPU Exception 6 pid=1 err=0x00000000 eip=0x00000007 cs=0x0000001b
  ebp=... esp=... useresp=... ustack: 8 words ... ubase=... utop=...
  heap=start-brk-end [cr2=... on #PF]
```

Run QEMU with `-serial file:serial.log` (or `-serial stdio`) and drive it headless via the monitor (`-monitor stdio`, `sendkey`, `screendump`). That's how the fork-corruption bugs got caught — `eip=0x7` with `ebp` sitting in the heap is unmistakable once you see the register dump next to the heap range.

## Honest limitations

- No memory isolation at all. Every task shares USER code/data/bss and the kernel heap. `fork` without an MMU can't give the kid private globals at the same virtual addresses, so hush's fork path (which assumes copy semantics) pollutes shared state. The vfork discipline + fault containment keeps the box alive, but a failed exec still kills that shell instance and respawns a fresh one — you'll lose `cwd`.
- `exec` ignores `argv`/`envp`. Non-`sh` programs always start as `sh`. Pipes/dup work at the fd level; job control doesn't exist.
- `wait4`'s `-2`/rewind and the `eip -= 2` assume the syscall instruction — true today, fragile forever.
- `kmalloc` has no locking; it survives because syscalls run with IF clear, but it's one `sti` in the wrong place away from corruption.
- The user heap is a fixed 1M window per task and `wait4` never frees shared heaps — long sessions leak.

Roadmap, roughly: per-process address spaces (then real fork, real exec with argv, and deleting half the hacks above), a disk driver so VFS outlives boot, signals past SIGKILL, and growing the applet set once exec actually forwards arguments.

MIT, (c) 2026 Ahmed Barakat.
