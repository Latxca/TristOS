#pragma once

#include <stdint.h>

namespace trist::clock {

struct DateTime {
    uint8_t second;
    uint8_t minute;
    uint8_t hour;
    uint8_t day;
    uint8_t month;
    uint8_t year;
};

void initialize();
bool set_from_text(const char* text);
void update_screen();
const DateTime& now();

}
