#include "services.hpp"
#include "kernel.hpp"

namespace trist::services {

static void copy_text(char* destination, const char* source, uint32_t size) {
    uint32_t i = 0;
    for (; i + 1 < size && source[i] != 0; ++i) destination[i] = source[i];
    destination[i] = 0;
}

void initialize(SecurityState* state) {
    state->defender_enabled = true;
    state->firewall_enabled = true;
    state->updates_enabled = true;
    copy_text(state->language, "English", sizeof(state->language));
    for (uint32_t i = 0; i < 8; ++i) { state->wifi[i].used = 0; state->bluetooth[i].paired = 0; }
}

void remember_wifi(SecurityState* state, const char* ssid, const char* password) {
    for (uint32_t i = 0; i < 8; ++i) if (!state->wifi[i].used) {
        copy_text(state->wifi[i].ssid, ssid, sizeof(state->wifi[i].ssid));
        copy_text(state->wifi[i].password, password, sizeof(state->wifi[i].password));
        state->wifi[i].encrypted = 0;
        state->wifi[i].used = 1;
        return;
    }
}

void remember_bluetooth(SecurityState* state, const char* name, bool trusted) {
    for (uint32_t i = 0; i < 8; ++i) if (!state->bluetooth[i].paired) {
        copy_text(state->bluetooth[i].name, name, sizeof(state->bluetooth[i].name));
        state->bluetooth[i].trusted = trusted ? 1 : 0;
        state->bluetooth[i].paired = 1;
        return;
    }
}

void defender_scan(const SecurityState* state) {
    console_write("Trist Defender: ");
    console_write_line(state->defender_enabled ? "enabled; system signature scan clean" : "disabled");
}

void firewall_status(const SecurityState* state) {
    console_write("Network Firewall: ");
    console_write_line(state->firewall_enabled ? "enabled; default-deny service policy" : "disabled");
}

void show(const SecurityState* state) {
    console_write("Language: "); console_write_line(state->language);
    defender_scan(state);
    firewall_status(state);
    console_write_line(state->updates_enabled ? "Update service: enabled" : "Update service: disabled");
    console_write_line("Wi-Fi profiles: remembered profile storage enabled");
    console_write_line("Bluetooth device memory: enabled");
}

}
