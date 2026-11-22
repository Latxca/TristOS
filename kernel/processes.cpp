#include "processes.hpp"
#include "kernel.hpp"

namespace trist::processes {

void initialize(Table* table) {
    table->count = 25;
    table->entries[0] = {1, "clockd.texe", true, true};
    table->entries[1] = {2, "netd.texe", true, true};
    table->entries[2] = {3, "deviced.texe", true, true};
    table->entries[3] = {4, "securityd.texe", true, true};
    table->entries[4] = {5, "indexerd.texe", true, false};
    table->entries[5] = {6, "automountd.texe", true, true};
    table->entries[6] = {7, "taskd.texe", true, true};
    table->entries[7] = {8, "wifid.texe", true, true};
    table->entries[8] = {9, "bluetoothd.texe", true, true};
    table->entries[9] = {10, "usbd.texe", true, true};
    table->entries[10] = {11, "firewalld.texe", true, true};
    table->entries[11] = {12, "defenderd.texe", true, true};
    table->entries[12] = {13, "updated.texe", true, true};
    table->entries[13] = {14, "wallpaperd.texe", true, false};
    table->entries[14] = {15, "languaged.texe", true, false};
    table->entries[15] = {16, "printd.texe", true, false};
    table->entries[16] = {17, "audiod.texe", true, false};
    table->entries[17] = {18, "videod.texe", true, false};
    table->entries[18] = {19, "photod.texe", true, false};
    table->entries[19] = {20, "pdfd.texe", true, false};
    table->entries[20] = {21, "backupd.texe", true, true};
    table->entries[21] = {22, "eventlogd.texe", true, true};
    table->entries[22] = {23, "appmodeld.texe", true, true};
    table->entries[23] = {24, "systemui.texe", false, true};
    table->entries[24] = {25, "tristexplorer.texe", false, false};
}

void show_background(const Table* table) {
    console_write_line("Background processes:");
    for (uint32_t i = 0; i < table->count; ++i) if (table->entries[i].background) {
        console_write("  ");
        console_write(table->entries[i].name);
        console_write_line(table->entries[i].protected_process ? " [protected]" : " [background]");
    }
}

}
