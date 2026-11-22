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
3. `kernel/main.cpp` is the C++ kernel entry point. It initializes VGA text output, mounts the TristOS NTFS-compatible volume model, registers system apps, starts the networking service, and runs the user setup flow.
4. `kernel/clock.cpp` reads the PC CMOS real-time clock and continuously renders a system-wide clock in the top-right of the VGA desktop.
5. `kernel/users.cpp` provides the local-user database, hidden disabled administrator record, `users` and `admins` groups, permissions, and first-boot OOBE.
6. `kernel/net.cpp` exposes the network-time synchronization hook used when networking is available; the RTC can otherwise be set by the user during OOBE.
7. `kernel/crypto.cpp` provides a freestanding ChaCha20 implementation using a 256-bit key. It is a stream cipher and requires authenticated storage before being used for untrusted data.
8. `kernel/fs.cpp` provides the initial in-memory volume and file-record layer.
9. `kernel/main.cpp` provides the keyboard-polled command prompt, taskbar, and Explorer file listing exposed by `tristexplorer.texe`.
10. `kernel/storage.cpp`, `kernel/devices.cpp`, and `kernel/media.cpp` provide installation-target selection plus service boundaries for USB, Wi-Fi, Bluetooth, video, photos, browser, and calculator applications.
11. `kernel/hardware.cpp` detects the CPU vendor with CPUID, totals usable E820 RAM, reads BIOS EDD storage capacity when available, parses SMBIOS motherboard information, and generates a system name.

The current filesystem is an **NTFS-compatible TristOS volume model**, not yet a driver for Microsoft NTFS partitions. Disk-sector I/O, journaling, partition installation, and real USB, Wi-Fi, Bluetooth, video, and network-device/NTP drivers are planned next. The current modules expose the kernel-facing service boundaries and report which hardware backends are unavailable.

## `.texe` applications

TristOS uses `.texe` as its application extension. The initial metadata format is `TRISTEXE`, `VERSION=1`, and an entry-point declaration.

- `apps/locksystem.texe` — lock-screen system app
- `apps/tristexplorer.texe` — shell and window-manager app
- `apps/netd.texe` — background networking service
- `apps/userinit.texe` — first-boot user setup service
- `apps/settings.texe` — settings and password manager
- `apps/webbrowser.texe` — web browser service
- `apps/calculator.texe` — calculator service
- `apps/photos.texe` — photo viewer service
- `apps/videos.texe` — video service

The kernel currently registers these applications as built-in services so the image remains small and freestanding. OOBE asks whether data should remain on the external boot device or target an internal SSD/HDD. The taskbar exposes Start, Explorer, Command Prompt, and Settings. On first boot, `userinit.texe` requires a user-created username, display name, password, and date/time in `HH:MM:SS DD/MM/YY` format. The built-in administrator record is hidden and disabled by default. The OOBE-created account is an administrator in `admins`; additional accounts can join `users`, which has read/execute permissions. The `admins` group grants read, write, execute, and user-management permissions. The command prompt supports `help`, `dir`, `explorer`, `settings`, `password`, `devices`, `taskmgr`, `browser`, `calc`, `photos`, `videos`, `time`, `ver`, and `exit`. `taskmgr` shows the built-in process list and hardware information. A disk-backed `.texe` loader and persistent user database can be added once filesystem block I/O exists.
