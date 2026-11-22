#include "storage.hpp"
#include "kernel.hpp"

namespace trist::storage {

void choose_install_target(InstallPlan* plan) {
    char answer[4];
    plan->system_drive = 'T';
    plan->external_drive = 'E';
    console_write_line("Choose where TristOS should store user data:");
    console_write_line("  E - keep data on the external boot device");
    console_write_line("  I - install data to the internal SSD/HDD");
    do {
        console_write("Storage choice [E/I]: ");
        console_read_line(answer, sizeof(answer));
    } while (answer[0] != 'e' && answer[0] != 'i');
    plan->target = answer[0] == 'e' ? InstallTarget::ExternalBootDevice : InstallTarget::InternalDrive;
    plan->valid = true;
}

void show_install_target(const InstallPlan* plan) {
    console_write("Storage target: ");
    console_write_line(plan->target == InstallTarget::ExternalBootDevice ? "E: external boot device" : "T: internal SSD/HDD");
    console_write_line("Main TristOS volume: T:; detected external volumes use E:, F:, G: and onward.");
    console_write_line("Disk partitioning and persistent block I/O are not enabled in this kernel yet.");
}

}
