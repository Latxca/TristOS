# TristOS

TristOS is a freestanding x86-64 hobby operating system with a BIOS boot path and a C++17 kernel.

> `C+` is not a standard language name; this implementation uses freestanding **C++**. The boot sector and CPU mode switch remain in NASM because firmware starts execution in 16-bit mode.

## Requirements

- NASM
- GNU Make
- a freestanding-capable x86-64 C++ compiler (`g++` or `x86_64-elf-g++`)
- GNU binutils (`ld`, `objcopy`)
- Python 3
- QEMU (`qemu-system-x86_64`)

## Build and run

```sh
make
make run
```

The raw image is `build/tristos.img`.

## Architecture

1. `boot/boot.asm` loads the recovery manager with BIOS INT 13h extensions.
2. `boot/stage2.asm` displays the recovery menu, loads the 64 KiB kernel image, creates identity-mapped paging structures, and enters long mode.
3. `kernel/main.cpp` is the C++ kernel entry point. It initializes VGA text output, mounts the TristOS NTFS-compatible volume model, registers system apps, and starts the networking service.
4. `kernel/crypto.cpp` provides a freestanding ChaCha20 implementation using a 256-bit key. It is a stream cipher and requires authenticated storage before being used for untrusted data.
5. `kernel/fs.cpp` provides the initial in-memory volume and file-record layer.

The current filesystem is an **NTFS-compatible TristOS volume model**, not yet a driver for Microsoft NTFS partitions. Disk-sector I/O, journaling, permissions, and a real network-device driver are planned next; the current network module exposes the kernel-facing state and service boundary.

## `.texe` applications

TristOS uses `.texe` as its application extension. The initial metadata format is `TRISTEXE`, `VERSION=1`, and an entry-point declaration.

- `apps/locksystem.texe` — lock-screen system app
- `apps/tristexplorer.texe` — shell and window-manager app
- `apps/netd.texe` — background networking service

The kernel currently registers these applications as built-in services so the image remains small and freestanding. A disk-backed `.texe` loader can be added once filesystem block I/O exists.
