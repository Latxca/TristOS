#pragma once

#include <stdint.h>

namespace trist::hardware {

struct SystemInfo {
    char cpu_vendor[13];
    char motherboard_vendor[64];
    char motherboard_model[64];
    char system_name[64];
    uint64_t ram_bytes;
    uint64_t storage_bytes;
    bool storage_known;
};

void detect(SystemInfo* info);
void show(const SystemInfo* info);
void task_manager(const SystemInfo* info);

}
