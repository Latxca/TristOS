#include "devices.hpp"
#include "kernel.hpp"

namespace trist::devices {

void initialize(DeviceState* state) {
    state->usb = false;
    state->wifi = false;
    state->bluetooth = false;
    state->video = true;
    state->photo = true;
}

void show_status(const DeviceState* state) {
    console_write_line(state->usb ? "USB: controller online" : "USB: driver waiting");
    console_write_line(state->wifi ? "Wi-Fi: connected" : "Wi-Fi: adapter not detected");
    console_write_line(state->bluetooth ? "Bluetooth: available" : "Bluetooth: adapter not detected");
    console_write_line(state->video ? "Video: VGA framebuffer online" : "Video: unavailable");
    console_write_line(state->photo ? "Photos: image service online" : "Photos: unavailable");
}

}
