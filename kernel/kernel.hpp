#pragma once

#include <stdint.h>

namespace trist {

struct AppDescriptor {
    const char* name;
    const char* description;
    void (*entry)();
};

void console_init();
void console_write(const char* text);
void console_write_line(const char* text);
void console_write_hex(uint64_t value);
void console_read_line(char* buffer, uint32_t capacity, bool echo = true);
void lock_screen();
void command_prompt();
void settings_app();
void explorer();

}
