#pragma once

#include <stdint.h>

namespace trist::processes {

struct Process { uint32_t pid; const char* name; bool background; bool protected_process; };
struct Table { Process entries[25]; uint32_t count; };

void initialize(Table* table);
void show_background(const Table* table);

}
