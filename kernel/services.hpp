#pragma once

#include <stdint.h>

namespace trist::services {

struct WifiProfile { char ssid[32]; char password[64]; uint8_t encrypted; uint8_t used; };
struct BluetoothDevice { char name[32]; uint8_t paired; uint8_t trusted; };
struct SecurityState { bool defender_enabled; bool firewall_enabled; bool updates_enabled; char language[16]; WifiProfile wifi[8]; BluetoothDevice bluetooth[8]; };

void initialize(SecurityState* state);
void show(const SecurityState* state);
void remember_wifi(SecurityState* state, const char* ssid, const char* password);
void remember_bluetooth(SecurityState* state, const char* name, bool trusted);
void defender_scan(const SecurityState* state);
void firewall_status(const SecurityState* state);

}
