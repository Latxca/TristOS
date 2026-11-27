#pragma once

#include "../net.hpp"

#include <stddef.h>
#include <stdint.h>

namespace tristos::drivers::e1000 {

inline constexpr uint16_t kVendorIntel        = 0x8086u;
inline constexpr uint16_t kDevice82540Em      = 0x100Eu; // Standard QEMU e1000
inline constexpr uint16_t kDevice82545Em      = 0x100Fu;
inline constexpr uint16_t kDevice82574L       = 0x10D3u;

inline constexpr size_t kRxRingSize         = 32;
inline constexpr size_t kTxRingSize         = 32;
inline constexpr size_t kRxBufferBytes      = 2048;
inline constexpr size_t kTxBufferBytes      = 2048;

enum class Status : uint8_t {
    Ok = 0,
    InvalidArg,
    AlreadyInitialized,
    NotInitialized,
    IoSpaceUnsupported,
    LinkDown,
    TxTimeout
};

struct Config {
    volatile uint8_t* mmio_base; // Mapped BAR0 virtual base address
    uint64_t hhdm_offset;        // Higher-half direct map offset (0 if identity-mapped)
    tristos::net::Ipv4Address ip;
    tristos::net::Ipv4Address netmask;
    tristos::net::Ipv4Address gateway;
    bool auto_init_net;          // Automatically call tristos::net::init() on success
};

Status init(const Config& config);
bool is_initialized();
void poll_link_status();

tristos::net::MacAddress mac_address();
bool link_up();
tristos::net::DriverOps driver_ops();

bool transmit_frame(void* ctx, const uint8_t* frame, size_t len);
size_t poll_receive_frame(void* ctx, uint8_t* out_frame, size_t max_len);

const char* status_to_string(Status status);

} // namespace tristos::drivers::e1000
