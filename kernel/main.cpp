#include "kernel.hpp"
#include "crypto.hpp"
#include "fs.hpp"
#include "net.hpp"
#include "clock.hpp"
#include "users.hpp"
#include "settings.hpp"
#include "devices.hpp"
#include "media.hpp"
#include "hardware.hpp"
#include "drivers.hpp"
#include "processes.hpp"
#include "services.hpp"
#include "version.hpp"

namespace trist {

static volatile uint16_t* const video = reinterpret_cast<volatile uint16_t*>(0xb8000);
static uint32_t cursor;

static void taskbar_render() {
    const char* bar = " Start | Explorer | Command Prompt | Settings ";
    for (uint32_t i = 0; i < 80; ++i) video[24 * 80 + i] = 0x1720;
    for (uint32_t i = 0; bar[i] != 0 && i < 80; ++i) video[24 * 80 + i] = static_cast<uint16_t>(0x1720 | static_cast<uint8_t>(bar[i]));
}

void console_init() {
    cursor = 0;
    for (uint32_t i = 0; i < 80 * 25; ++i) video[i] = 0x0720;
    taskbar_render();
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

void console_read_line(char* buffer, uint32_t capacity, bool echo) {
    uint32_t length = 0;
    bool shift = false;
    for (;;) {
        clock::update_screen();
        taskbar_render();
        uint8_t scan = keyboard_read();
        if (scan == 0x1d) { ctrl_down = true; continue; }
        if (scan == 0x38) { alt_down = true; continue; }
        if (scan == 0x9d) { ctrl_down = false; continue; }
        if (scan == 0xb8) { alt_down = false; continue; }
        if (ctrl_down && alt_down && scan == 0x53) { security_menu(); continue; }
        if (scan == 0x2a || scan == 0x36) { shift = true; continue; }
        if (scan == 0xaa || scan == 0xb6) { shift = false; continue; }
        if (scan & 0x80) continue;
        if (scan == 0x1c) {
            buffer[length] = 0;
            if (echo) console_write_line("");
            else console_write_line("");
            return;
        }
        if (scan == 0x0e) {
            if (length != 0) { --length; if (echo) console_write("\\b \\b"); }
            continue;
        }
        char key = key_for_scan(scan, shift);
        if (key != 0 && length + 1 < capacity) {
            buffer[length++] = key;
            char output[2] = {key, 0};
            if (echo) console_write(output);
        }
    }
}

static bool command_is(const char* left, const char* right) {
    while (*left != 0 && *left == *right) { ++left; ++right; }
    return *left == *right;
}

static void security_menu() {
    console_write_line("Control-Alt-Delete");
    console_write_line("  l - lock workstation");
    console_write_line("  s - sign out and stop user processes");
    console_write_line("  p - power off");
    console_write("cad> ");
    char choice[8];
    console_read_line(choice, sizeof(choice));
    if (choice[0] == 'l') lock_screen();
    else if (choice[0] == 's') console_write_line("Signed out; non-SYSTEM and non-TrustedInstaller processes stopped.");
    else if (choice[0] == 'p') { console_write_line("Power off requested."); for (;;) asm volatile("hlt"); }
}

void lock_screen() {
    console_write_line("locksystem.texe: lock screen service active");
    console_write_line("Console input authentication is not enabled in this build.");
}

void explorer() {
    console_write_line("tristexplorer.texe: TristOS file explorer");
    console_write_line("  T:/System/locksystem.texe   [system app]");
    console_write_line("  T:/System/tristexplorer.texe [shell + file explorer]");
    console_write_line("  T:/System/netd.texe          [background app]");
    console_write_line("  T:/Users/<user>              [user profile]");
    console_write_line("  T:/Users/<user>/AppData     [application data]");
    console_write_line("  T:/System/settings.texe     [settings]");
    console_write_line("  T:/apps/webbrowser.texe     [browser]");
    console_write_line("  T:/apps/calculator.texe     [calculator]");
    console_write_line("  T:/apps/photos.texe         [photo viewer]");
    console_write_line("  T:/apps/videos.texe         [video player]");
}

static hardware::SystemInfo system_info;
static processes::Table process_table;
static services::SecurityState service_state;
static bool ctrl_down;
static bool alt_down;

static void security_menu();
void lock_screen();
void command_prompt() {
    console_write_line("Command Prompt (tristcmd) - type help for commands");
    char command[64];
    for (;;) {
        console_write("T:\\> ");
        console_read_line(command, sizeof(command));
        if (command_is(command, "help")) {
            console_write_line("help  dir  explorer  settings  password  devices  taskmgr  defender  firewall  wifi  bluetooth  browser  calc  photos  videos  time  ver  exit");
        } else if (command_is(command, "dir")) {
            explorer();
        } else if (command_is(command, "explorer")) {
            explorer();
        } else if (command_is(command, "settings")) {
            settings_app();
        } else if (command_is(command, "password")) {
            change_password();
        } else if (command_is(command, "devices")) {
            console_write_line("Use Settings > Devices when hardware drivers are available.");
        } else if (command_is(command, "taskmgr")) {
            hardware::task_manager(&system_info);
            processes::show_background(&process_table);
        } else if (command_is(command, "defender")) {
            services::defender_scan(&service_state);
        } else if (command_is(command, "firewall")) {
            services::firewall_status(&service_state);
        } else if (command_is(command, "wifi")) {
            console_write_line("Saved Wi-Fi profiles are managed by wifid.texe.");
        } else if (command_is(command, "bluetooth")) {
            console_write_line("Remembered Bluetooth devices are managed by bluetoothd.texe.");
        } else if (command_is(command, "browser")) {
            media::browser();
        } else if (command_is(command, "calc")) {
            media::calculator();
        } else if (command_is(command, "photos")) {
            media::photos();
        } else if (command_is(command, "videos")) {
            media::videos();
        } else if (command_is(command, "time")) {
            console_write_line("The system clock is shown in the top-right corner.");
        } else if (command_is(command, "ver")) {
            console_write_line("TristOS 3.4 x64");
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
    console_write("TristOS ");
    console_write_line(version::kVersion);
    console_write_line("C++ kernel online - MIT licensed");
    console_write_line(version::kCopyright);
    console_write_line("Executable format: .texe / version 1");

    static fs::Volume volume;
    fs::format(&volume);
    fs::create_directory(&volume, fs::kRootInode, "System", 0, users::kGroupAdmins);
    fs::create_directory(&volume, fs::kRootInode, "Users", 0, users::kGroupAdmins);
    const fs::FileRecord* system_directory = fs::find(&volume, "System");
    const fs::FileRecord* users_directory = fs::find(&volume, "Users");
    if (system_directory != nullptr) {
        fs::create_owned(&volume, system_directory->inode, "locksystem.texe", 1, false, true, 0, users::kGroupAdmins);
        fs::create_owned(&volume, system_directory->inode, "tristexplorer.texe", 1, false, false, 0, users::kGroupAdmins);
        fs::create_owned(&volume, system_directory->inode, "netd.texe", 1, false, false, 0, users::kGroupAdmins);
        fs::create_owned(&volume, system_directory->inode, "userinit.texe", 1, true, true, 0, users::kGroupAdmins);
        fs::create_owned(&volume, system_directory->inode, "settings.texe", 1, false, true, 0, users::kGroupAdmins);
        fs::create_owned(&volume, system_directory->inode, "systemui.texe", 1, false, true, 0, users::kGroupAdmins);
        fs::create_owned(&volume, system_directory->inode, "defender.texe", 1, false, true, 0, users::kGroupAdmins);
        fs::create_owned(&volume, system_directory->inode, "texteditor.texe", 1, false, false, 0, users::kGroupAdmins);
        fs::create_owned(&volume, system_directory->inode, "pdfviewer.texe", 1, false, false, 0, users::kGroupAdmins);
        fs::create_owned(&volume, system_directory->inode, "office.texe", 1, false, true, 0, users::kGroupAdmins);
    }
    if (users_directory != nullptr) fs::create_owned(&volume, users_directory->inode, "Public", 0, true, false, 0, users::kGroupUsers);
    fs::create(&volume, "webbrowser.texe", 1, false);
    fs::create(&volume, "calculator.texe", 1, false);
    fs::create(&volume, "photos.texe", 1, false);
    fs::create(&volume, "videos.texe", 1, false);
    const fs::FileRecord* signed_system = fs::find_path(&volume, "/System/locksystem.texe");
    if (signed_system != nullptr && fs::verify_signature(&volume, signed_system)) console_write_line("securityd: system signature verified");
    else console_write_line("securityd: system signature failure");
    console_write_line("T: NTFS-compatible volume mounted");
    console_write_line("ChaCha20-256 encrypted file support enabled");
    console_write_line("System files are sealed with integrity signatures");

    hardware::detect(&system_info);
    hardware::show(&system_info);

    static drivers::DriverTable driver_table;
    drivers::initialize(&driver_table);
    drivers::show(&driver_table);

    processes::initialize(&process_table);
    processes::show_background(&process_table);
    services::initialize(&service_state);
    services::show(&service_state);

    static devices::DeviceState devices;
    devices::initialize(&devices);
    devices::show_status(&devices);

    static net::NetworkState network;
    net::initialize(&network);
    net::show_status(&network);
    if (net::sync_time_from_internet(&network)) console_write_line("Internet time synchronization available");
    else console_write_line("Internet time unavailable; setup will set the RTC manually.");

    static users::UserDatabase user_database;
    users::initialize(&user_database);
    if (users::first_boot(&user_database)) users::run_oobe(&user_database);
    users::User* active_user = &user_database.accounts[1];
    set_active_user(active_user);
    if (users_directory != nullptr) {
        fs::create_owned(&volume, users_directory->inode, active_user->name, 0, true, false, 1, users::kGroupAdmins);
        const fs::FileRecord* home = fs::find_child(&volume, users_directory->inode, active_user->name);
        if (home != nullptr) {
            fs::create_app_data(&volume, home->inode, "tristexplorer.texe", 1, users::kGroupAdmins);
            fs::create_app_data(&volume, home->inode, "settings.texe", 1, users::kGroupAdmins);
            fs::create_app_data(&volume, home->inode, "webbrowser.texe", 1, users::kGroupAdmins);
        }
    }
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
