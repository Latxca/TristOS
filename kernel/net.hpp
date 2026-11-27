#pragma once

#include <stddef.h>
#include <stdint.h>

namespace tristos::net {

inline constexpr size_t kMacAddrLen = 6;
inline constexpr size_t kIpv4AddrLen = 4;
inline constexpr size_t kEthernetHeaderLen = 14;
inline constexpr size_t kIpv4MinHeaderLen = 20;
inline constexpr size_t kUdpHeaderLen = 8;
inline constexpr size_t kIcmpMinHeaderLen = 8;
inline constexpr size_t kMaxFrameBytes = 1536; // Ethernet frame + headroom
inline constexpr size_t kMaxPacketPool = 32;
inline constexpr size_t kArpTableSize = 16;

// Common EtherTypes (host-order values converted to big-endian on the wire)
inline constexpr uint16_t kEtherTypeIpv4 = 0x0800u;
inline constexpr uint16_t kEtherTypeArp  = 0x0806u;

// IPv4 protocol numbers
inline constexpr uint8_t kIpProtoIcmp = 1u;
inline constexpr uint8_t kIpProtoUdp  = 17u;

// ICMP message types
inline constexpr uint8_t kIcmpEchoReply   = 0u;
inline constexpr uint8_t kIcmpEchoRequest = 8u;

enum class Status : uint8_t {
    Ok = 0,
    InvalidArg,
    NotInitialized,
    NoBuffer,
    LinkDown,
    ArpPending,
    Dropped,
    BufferTooSmall,
    TransmitFailed
};

struct MacAddress {
    uint8_t bytes[kMacAddrLen];
};

struct Ipv4Address {
    uint8_t bytes[kIpv4AddrLen];
};

struct Packet {
    uint8_t data[kMaxFrameBytes];
    size_t length;
    bool in_use;
};

struct ArpEntry {
    Ipv4Address ip;
    MacAddress mac;
    bool valid;
};

struct NetConfig {
    MacAddress mac;
    Ipv4Address ip;
    Ipv4Address netmask;
    Ipv4Address gateway;
    bool link_up;
};

using TransmitFrameFn = bool (*)(void* driver_ctx, const uint8_t* frame, size_t len);
using PollReceiveFn  = size_t (*)(void* driver_ctx, uint8_t* out_frame, size_t max_len);

struct DriverOps {
    void* ctx;
    TransmitFrameFn transmit;
    PollReceiveFn poll_receive;
};

using UdpRecvCallback = void (*)(void* user_ctx,
                                 Ipv4Address src_ip,
                                 uint16_t src_port,
                                 uint16_t dst_port,
                                 const uint8_t* payload,
                                 size_t len);

// Initialization, link state, and configuration
Status init(const NetConfig& config, const DriverOps& ops);
bool is_initialized();
void set_link_up(bool up);
const NetConfig& config();

// Packet buffer pool helpers
Packet* alloc_packet();
void free_packet(Packet* pkt);
size_t free_packet_count();

// Poll NIC receive queue and process Ethernet / ARP / IPv4 / ICMP / UDP frames
void poll();

// ARP cache lookup and outbound ARP request
bool arp_lookup(Ipv4Address ip, MacAddress* out_mac);
Status send_arp_request(Ipv4Address target_ip);
const ArpEntry* arp_table(size_t* out_count);

// Outbound ICMP Echo and UDP datagrams
Status send_icmp_echo_reply(Ipv4Address dst_ip,
                            uint16_t identifier,
                            uint16_t sequence,
                            const uint8_t* payload,
                            size_t len);

Status send_udp(Ipv4Address dst_ip,
                uint16_t src_port,
                uint16_t dst_port,
                const uint8_t* payload,
                size_t len);

void set_udp_callback(UdpRecvCallback cb, void* user_ctx);

// Network byte-order helpers
uint16_t htons(uint16_t val);
uint16_t ntohs(uint16_t val);
uint32_t htonl(uint32_t val);
uint32_t ntohl(uint32_t val);

// Internet ones'-complement checksum helper
uint16_t internet_checksum(const uint8_t* data, size_t len);

const char* status_to_string(Status status);

} // namespace tristos::net
