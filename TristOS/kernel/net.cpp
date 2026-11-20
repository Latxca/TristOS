#include "net.hpp"
#include "kernel.hpp"

namespace trist::net {

void initialize(NetworkState* state) {
    state->link_up = false;
    state->address = {{192, 168, 1, 100}};
    state->gateway = {{192, 168, 1, 1}};
}

void show_status(const NetworkState* state) {
    console_write("Network: ");
    console_write(state->link_up ? "link up" : "driver waiting");
    console_write("; IPv4 ");
    console_write_hex(state->address.octets[0]);
    console_write(".");
    console_write_hex(state->address.octets[1]);
    console_write(".");
    console_write_hex(state->address.octets[2]);
    console_write(".");
    console_write_hex(state->address.octets[3]);
    console_write_line("");
}

}
