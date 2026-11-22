#pragma once

namespace trist::devices {

struct DeviceState { bool usb; bool wifi; bool bluetooth; bool video; bool photo; };

void initialize(DeviceState* state);
void show_status(const DeviceState* state);

}
