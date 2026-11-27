# TristOS

**TristOS 3.6** — Copyright Aaron Sumler 2026 — released under the TristOS Proprietary Source-Available License. See [`LICENSE`](LICENSE).

TristOS is a freestanding x86-64 hobby operating system with a BIOS boot path and a C++17 kernel.

> `C+` is not a standard language name; this implementation uses freestanding **C++**. The boot sector and CPU mode switch remain in NASM because firmware starts execution in 16-bit mode.

## Requirements

- NASM
- GNU Make
- a freestanding-capable x86-64 C++ compiler (`g++` or `x86_64-elf-g++`)
- GNU binutils (`ld`, `objcopy`)
- xorriso
- Python 3
- QEMU (`qemu-system-x86_64`)

## Build and run

```sh
make
make run
```

The bootable ISO is `build/tristos.iso`.

## Architecture

1. `boot/boot.asm` loads the recovery manager with BIOS INT 13h extensions. Holding either Shift key while powering on selects the recovery menu; a normal boot starts TristOS directly.
2. `boot/stage2.asm` displays the recovery menu, loads the 64 KiB kernel image, creates identity-mapped paging structures, and enters long mode.
3. `kernel/main.cpp` is the C++ kernel entry point. It initializes VGA text output, mounts the TristOS NTFS-compatible volume model, registers system apps, starts the networking service, and runs the user setup flow.
4. `kernel/clock.cpp` reads the PC CMOS real-time clock and continuously renders a system-wide clock in the top-right of the VGA desktop.
5. `kernel/users.cpp` provides the local-user database, hidden disabled administrator record, `users` and `admins` groups, permissions, and first-boot OOBE.
6. `kernel/net.cpp` exposes the network-time synchronization hook used when networking is available; the RTC can otherwise be set by the user during OOBE.
7. `kernel/crypto.cpp` provides a freestanding ChaCha20 implementation using a 256-bit key. It is a stream cipher and requires authenticated storage before being used for untrusted data.
8. `kernel/fs.cpp` provides a hierarchical NTFS-style volume model with root, System, Users, user-home, and per-application AppData directories; inode-like records; owner/group IDs; inherited default ACLs; DACL entries; encrypted and directory attributes; and permission checks. Applications can create private data under `/Users/<user>/AppData/Local/<app>` with `create_app_data`.
9. `kernel/main.cpp` provides the keyboard-polled command prompt, taskbar, and Explorer file listing exposed by `tristexplorer.texe`. The main volume is `T:` and the shell prompt uses `T:\\>`; the first detected external volume is assigned `E:`.
10. `kernel/storage.cpp`, `kernel/devices.cpp`, and `kernel/media.cpp` provide installation-target selection plus service boundaries for USB, Wi-Fi, Bluetooth, video, photos, browser, and calculator applications.
11. `kernel/drivers.cpp` registers generic VGA, PS/2, BIOS-disk, USB, Wi-Fi, Bluetooth, ATA, and network driver fallbacks.
12. `kernel/processes.cpp` registers 25 process slots, including more than 20 background services for Wi-Fi, Bluetooth, USB, firewall, Defender, updates, wallpaper, language, media, backups, event logging, and application management.
13. `kernel/services.cpp` provides Trist Defender, firewall status, English language selection, and remembered Wi-Fi/Bluetooth profile storage.
11. `kernel/hardware.cpp` detects the CPU vendor with CPUID, totals usable E820 RAM, reads BIOS EDD storage capacity when available, parses SMBIOS motherboard information, and generates a system name.

The current filesystem is an **NTFS-style TristOS volume model**, not yet a driver for Microsoft NTFS partitions. `T:` is the main TristOS volume and external media begin at `E:`. It now models Windows-NT-like ownership concepts: owner user, owner group, allow/deny ACL entries, inheritance-ready security descriptors, directory records, and access checks. System-owned files receive integrity signatures and `securityd` verifies protected system files during startup. Disk-sector I/O, journaling, persistent security descriptors, partition installation, and real USB, Wi-Fi, Bluetooth, video, and network-device/NTP drivers are planned next. The current modules expose the kernel-facing service boundaries and report which hardware backends are unavailable.

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
- `apps/systemui.texe` — desktop/taskbar screen application
- `apps/defender.texe` — built-in anti-virus service
- `apps/texteditor.texe` — text editor
- `apps/pdfviewer.texe` — PDF viewer
- `apps/office.texe` — business document suite
- `apps/controlpanel.texe` — control panel
- `apps/backup.texe` — backup utility
- `apps/eventviewer.texe` — event viewer
- `apps/calendar.texe` — calendar
- `apps/contacts.texe` — contacts
- `lang/en.lang` — the currently installed English language pack; other language packs are not installed yet
- `apps/notepad.exe` — PE compatibility manifest for the text editor

The kernel currently registers these applications as built-in services so the image remains small and freestanding. `systemui.texe` is the desktop screen application; the kernel is not itself treated as the desktop shell. TristOS recognizes both `.texe` and DOS/PE-style `.exe` image headers, although full user-mode PE execution is not enabled yet. OOBE asks whether data should remain on the external boot device or target an internal SSD/HDD. The taskbar exposes Start, Explorer, Command Prompt, and Settings. On first boot, `userinit.texe` requires a user-created username, display name, password, and date/time in `HH:MM:SS DD/MM/YY` format. The built-in administrator record is hidden and disabled by default. The OOBE-created account is an administrator in `admins`; additional accounts can join `users`, which has read/execute permissions. The `admins` group grants read, write, execute, and user-management permissions. The command prompt supports `help`, `dir`, `explorer`, `settings`, `password`, `devices`, `taskmgr`, `defender`, `firewall`, `wifi`, `bluetooth`, `browser`, `calc`, `photos`, `videos`, `time`, `ver`, and `exit`. Ctrl+Alt+Delete opens a lock, sign-out, and power-off menu. Wi-Fi and Bluetooth profile APIs remember profile credentials/device records in the current service state; persistent encrypted credential storage requires the future disk-backed key store. `taskmgr` shows the built-in process list and hardware information. A disk-backed `.texe` loader and persistent user database can be added once filesystem block I/O exists.
