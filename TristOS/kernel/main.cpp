#include "kernel.hpp"
#include "crypto.hpp"
#include "fs.hpp"
#include "net.hpp"

namespace trist {

static volatile uint16_t* const video = reinterpret_cast<volatile uint16_t*>(0xb8000);
static uint32_t cursor;

void console_init() {
    cursor = 0;
    for (uint32_t i = 0; i < 80 * 25; ++i) video[i] = 0x0720;
}

void console_write(const char* text) {
    while (*text != 0 && cursor < 80 * 25) {
        if (*text == '\n') cursor = ((cursor / 80) + 1) * 80;
        else video[cursor++] = static_cast<uint16_t>(0x0700 | static_cast<uint8_t>(*text));
        ++text;
    }
}

void console_write_line(const char* text) {
    console_write(text);
    console_write("\n");
}

void console_write_hex(uint64_t value) {
    const char digits[] = "0123456789abcdef";
    char output[17];
    output[16] = 0;
    for (int i = 15; i >= 0; --i) { output[i] = digits[value & 0xf]; value >>= 4; }
    console_write(output);
}

void lock_screen() {
    console_write_line("locksystem.texe: lock screen service active");
    console_write_line("Console input authentication is not enabled in this build.");
}

void explorer() {
    console_write_line("tristexplorer.texe: TristOS shell/window manager");
    console_write_line("  /system/locksystem.texe  [system app]");
    console_write_line("  /system/tristexplorer.texe [shell]");
    console_write_line("  /system/netd.texe          [background app]");
}

extern "C" void _start() __attribute__((section(".text._start"), noreturn));
extern "C" void _start() {
    console_init();
    console_write_line("TristOS C++ kernel online");
    console_write_line("Executable format: .texe / version 1");

    static fs::Volume volume;
    fs::format(&volume);
    fs::create(&volume, "locksystem.texe", 1, true);
    fs::create(&volume, "tristexplorer.texe", 1, false);
    fs::create(&volume, "netd.texe", 1, false);
    console_write_line("NTFS-compatible volume mounted");
    console_write_line("ChaCha20-256 encrypted file support enabled");

    static net::NetworkState network;
    net::initialize(&network);
    net::show_status(&network);
    lock_screen();
    explorer();
    for (;;) { asm volatile("hlt"); }
}

}
