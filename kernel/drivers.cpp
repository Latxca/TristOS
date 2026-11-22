#include "drivers.hpp"
#include "kernel.hpp"

namespace trist::drivers {

void initialize(DriverTable* table) {
    table->count = 8;
    table->entries[0] = {"generic-vga", true, true};
    table->entries[1] = {"generic-ps2-keyboard", true, true};
    table->entries[2] = {"generic-bios-disk", true, true};
    table->entries[3] = {"generic-usb", false, true};
    table->entries[4] = {"generic-wifi", false, true};
    table->entries[5] = {"generic-bluetooth", false, true};
    table->entries[6] = {"generic-ata-storage", false, true};
    table->entries[7] = {"generic-network", false, true};
}

void show(const DriverTable* table) {
    console_write_line("Generic driver registry:");
    for (uint32_t i = 0; i < table->count; ++i) {
        console_write("  ");
        console_write(table->entries[i].name);
        console_write_line(table->entries[i].present ? " [active]" : " [fallback/undetected]");
    }
}

}
