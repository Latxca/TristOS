#include "kernel.hpp"
#include "crypto.hpp"
#include "fs.hpp"
#include "net.hpp"
#include "clock.hpp"
#include "users.hpp"

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

static uint8_t keyboard_read() {
    uint8_t status;
    do { asm volatile("inb %1, %0" : "=a"(status) : "Nd"(static_cast<uint16_t>(0x64))); } while ((status & 1) == 0);
    uint8_t scan_code;
    asm volatile("inb %1, %0" : "=a"(scan_code) : "Nd"(static_cast<uint16_t>(0x60)));
    return scan_code;
}

static char key_for_scan(uint8_t scan, bool shift) {
    static const char numbers[] = "1234567890";
    static const char top[] = "qwertyuiop";
    static const char home[] = "asdfghjkl";
    static const char bottom[] = "zxcvbnm";
    if (scan >= 0x02 && scan <= 0x0b) return numbers[scan - 0x02];
    if (scan >= 0x10 && scan <= 0x19) return top[scan - 0x10];
    if (scan >= 0x1e && scan <= 0x26) return home[scan - 0x1e];
    if (scan >= 0x2c && scan <= 0x32) return bottom[scan - 0x2c];
    if (scan == 0x39) return ' ';
    if (scan == 0x27) return shift ? ':' : ';';
    if (scan == 0x33) return ',';
    if (scan == 0x34) return '.';
    if (scan == 0x35) return '/';
    return 0;
}

void console_read_line(char* buffer, uint32_t capacity) {
    uint32_t length = 0;
    bool shift = false;
    for (;;) {
        clock::update_screen();
        uint8_t scan = keyboard_read();
        if (scan == 0x2a || scan == 0x36) { shift = true; continue; }
        if (scan == 0xaa || scan == 0xb6) { shift = false; continue; }
        if (scan & 0x80) continue;
        if (scan == 0x1c) {
            buffer[length] = 0;
            console_write_line("");
            return;
        }
        if (scan == 0x0e) {
            if (length != 0) { --length; console_write("\\b \\b"); }
            continue;
        }
        char key = key_for_scan(scan, shift);
        if (key != 0 && length + 1 < capacity) {
            buffer[length++] = key;
            char output[2] = {key, 0};
            console_write(output);
        }
    }
}

static bool command_is(const char* left, const char* right) {
    while (*left != 0 && *left == *right) { ++left; ++right; }
    return *left == *right;
}

void lock_screen() {
    console_write_line("locksystem.texe: lock screen service active");
    console_write_line("Console input authentication is not enabled in this build.");
}

void explorer() {
    console_write_line("tristexplorer.texe: TristOS file explorer");
    console_write_line("  /system/locksystem.texe   [system app]");
    console_write_line("  /system/tristexplorer.texe [shell + file explorer]");
    console_write_line("  /system/netd.texe          [background app]");
    console_write_line("  /Users/Trist               [user profile]");
}

void command_prompt() {
    console_write_line("Command Prompt (tristcmd) - type help for commands");
    char command[64];
    for (;;) {
        console_write("C:\\> ");
        console_read_line(command, sizeof(command));
        if (command_is(command, "help")) {
            console_write_line("help  dir  explorer  time  ver  exit");
        } else if (command_is(command, "dir")) {
            explorer();
        } else if (command_is(command, "explorer")) {
            explorer();
        } else if (command_is(command, "time")) {
            console_write_line("The system clock is shown in the top-right corner.");
        } else if (command_is(command, "ver")) {
            console_write_line("TristOS 1.0 x64");
        } else if (command_is(command, "exit")) {
            return;
        } else if (command[0] != 0) {
            console_write_line("Command not found.");
        }
    }
}

extern "C" void _start() __attribute__((section(".text._start"), noreturn));
extern "C" void _start() {
    console_init();
    clock::initialize();
    console_write_line("TristOS C++ kernel online");
    console_write_line("Executable format: .texe / version 1");

    static fs::Volume volume;
    fs::format(&volume);
    fs::create(&volume, "locksystem.texe", 1, true);
    fs::create(&volume, "tristexplorer.texe", 1, false);
    fs::create(&volume, "netd.texe", 1, false);
    fs::create(&volume, "userinit.texe", 1, true);
    console_write_line("NTFS-compatible volume mounted");
    console_write_line("ChaCha20-256 encrypted file support enabled");

    static net::NetworkState network;
    net::initialize(&network);
    net::show_status(&network);
    if (net::sync_time_from_internet(&network)) console_write_line("Internet time synchronization available");
    else console_write_line("Internet time unavailable; setup will set the RTC manually.");

    static users::UserDatabase user_database;
    users::initialize(&user_database);
    if (users::first_boot(&user_database)) users::run_oobe(&user_database);
    users::show_accounts(&user_database);
    users::show_groups();

    lock_screen();
    explorer();
    command_prompt();
    for (;;) {
        clock::update_screen();
        for (uint32_t delay = 0; delay < 1000000; ++delay) asm volatile("pause");
    }
}

}
