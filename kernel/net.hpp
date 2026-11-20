#pragma once

#include <stdint.h>

namespace trist::net {

struct IPv4Address { uint8_t octets[4]; };
struct NetworkState {
    bool link_up;
    IPv4Address address;
    IPv4Address gateway;
};

void initialize(NetworkState* state);
bool sync_time_from_internet(const NetworkState* state);
void show_status(const NetworkState* state);

}
