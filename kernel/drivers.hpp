#pragma once

#include <stdint.h>

namespace trist::drivers {

struct Driver { const char* name; bool present; bool generic; };
struct DriverTable { Driver entries[8]; uint32_t count; };

void initialize(DriverTable* table);
void show(const DriverTable* table);

}
