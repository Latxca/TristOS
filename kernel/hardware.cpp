#include "hardware.hpp"
#include "kernel.hpp"

namespace trist::hardware {

struct E820Entry { uint64_t base; uint64_t length; uint32_t type; uint32_t attributes; } __attribute__((packed));

static void copy_text(char* dst, const char* src, uint32_t capacity) {
    uint32_t i = 0;
    for (; i + 1 < capacity && src[i] != 0; ++i) dst[i] = src[i];
    dst[i] = 0;
}

static void append(char* dst, const char* src, uint32_t capacity) {
    uint32_t length = 0;
    while (length + 1 < capacity && dst[length] != 0) ++length;
    for (uint32_t i = 0; length + i + 1 < capacity && src[i] != 0; ++i) dst[length + i] = src[i];
    dst[length + 1 < capacity ? length : capacity - 1] = 0;
}

static void cpuid(uint32_t leaf, uint32_t* a, uint32_t* b, uint32_t* c, uint32_t* d) {
    asm volatile("cpuid" : "=a"(*a), "=b"(*b), "=c"(*c), "=d"(*d) : "a"(leaf));
}

static bool equal(const char* left, const char* right) {
    uint32_t i = 0;
    while (left[i] != 0 && left[i] == right[i]) ++i;
    return left[i] == 0 && right[i] == 0;
}

static const char* smbios_string(const uint8_t* structure, uint8_t index) {
    if (index == 0) return "Unknown";
    const char* text = reinterpret_cast<const char*>(structure + structure[1]);
    for (uint8_t current = 1; current < index && *text != 0; ++current) {
        while (*text != 0) ++text;
        ++text;
    }
    return *text == 0 ? "Unknown" : text;
}

static void detect_board(SystemInfo* info) {
    info->motherboard_vendor[0] = 0;
    info->motherboard_model[0] = 0;
    for (uintptr_t address = 0xf0000; address < 0x100000; address += 16) {
        const uint8_t* entry = reinterpret_cast<const uint8_t*>(address);
        if (entry[0] == '_' && entry[1] == 'S' && entry[2] == 'M' && entry[3] == '_') {
            const uint8_t* table = reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(*reinterpret_cast<const uint32_t*>(entry + 0x18)));
            uint16_t count = *reinterpret_cast<const uint16_t*>(entry + 0x1c);
            for (uint16_t i = 0; i < count && table[1] >= 4; ++i) {
                if (table[0] == 2) {
                    copy_text(info->motherboard_vendor, smbios_string(table, table[4]), sizeof(info->motherboard_vendor));
                    copy_text(info->motherboard_model, smbios_string(table, table[5]), sizeof(info->motherboard_model));
                    return;
                }
                const uint8_t* next = table + table[1];
                while (next[0] != 0 || next[1] != 0) ++next;
                table = next + 2;
            }
        }
    }
    copy_text(info->motherboard_vendor, "Unknown manufacturer", sizeof(info->motherboard_vendor));
    copy_text(info->motherboard_model, "Unknown motherboard", sizeof(info->motherboard_model));
}

void detect(SystemInfo* info) {
    uint32_t a, b, c, d;
    cpuid(0, &a, &b, &c, &d);
    *reinterpret_cast<uint32_t*>(info->cpu_vendor + 0) = b;
    *reinterpret_cast<uint32_t*>(info->cpu_vendor + 4) = d;
    *reinterpret_cast<uint32_t*>(info->cpu_vendor + 8) = c;
    info->cpu_vendor[12] = 0;
    info->ram_bytes = 0;
    uint16_t count = *reinterpret_cast<volatile uint16_t*>(0x7000);
    const E820Entry* entries = reinterpret_cast<const E820Entry*>(0x7002);
    for (uint16_t i = 0; i < count; ++i) if (entries[i].type == 1) info->ram_bytes += entries[i].length;
    uint64_t sectors = *reinterpret_cast<volatile uint64_t*>(0x7310);
    info->storage_known = sectors != 0;
    info->storage_bytes = sectors * 512;
    detect_board(info);
    info->system_name[0] = 0;
    append(info->system_name, "TRISTOS-", sizeof(info->system_name));
    if (equal(info->cpu_vendor, "AuthenticAMD")) append(info->system_name, "AMD-", sizeof(info->system_name));
    else if (equal(info->cpu_vendor, "GenuineIntel")) append(info->system_name, "INTEL-", sizeof(info->system_name));
    else append(info->system_name, "X64-", sizeof(info->system_name));
    append(info->system_name, info->motherboard_model, sizeof(info->system_name));
}

void show(const SystemInfo* info) {
    console_write("CPU vendor: "); console_write_line(info->cpu_vendor);
    console_write("RAM bytes: "); console_write_hex(info->ram_bytes); console_write_line("");
    console_write("Storage bytes: ");
    if (info->storage_known) console_write_hex(info->storage_bytes); else console_write_line("unknown");
    if (info->storage_known) console_write_line("");
    console_write("Motherboard manufacturer: "); console_write_line(info->motherboard_vendor);
    console_write("Motherboard model: "); console_write_line(info->motherboard_model);
    console_write("System name: "); console_write_line(info->system_name);
}

void task_manager(const SystemInfo* info) {
    console_write_line("Task Manager");
    console_write_line("  PID  NAME              STATE");
    console_write_line("  1    kernel            running");
    console_write_line("  2    locksystem.texe   running");
    console_write_line("  3    tristexplorer.texe running");
    console_write_line("  4    netd.texe         waiting");
    show(info);
}

}
