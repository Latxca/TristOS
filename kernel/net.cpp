#include "net.hpp"

namespace tristos::net {
namespace {

inline constexpr uint16_t kArpHtypeEthernet = 1u;
inline constexpr uint16_t kArpPtypeIpv4     = 0x0800u;
inline constexpr uint16_t kArpOpRequest     = 1u;
inline constexpr uint16_t kArpOpReply       = 2u;
inline constexpr size_t kArpFramePayloadLen = 28; // htype(2)+ptype(2)+hlen(1)+plen(1)+op(2)+sha(6)+spa(4)+tha(6)+tpa(4)

inline constexpr uint8_t kIpv4Version4      = 4u;
inline constexpr uint8_t kIpv4DefaultTtl    = 64u;

inline constexpr MacAddress kBroadcastMac = {{0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu}};
inline constexpr MacAddress kZeroMac      = {{0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u}};

NetConfig g_config{};
DriverOps g_ops{};
bool g_initialized = false;

Packet g_packet_pool[kMaxPacketPool];
ArpEntry g_arp_table[kArpTableSize];
size_t g_arp_next_slot = 0;

UdpRecvCallback g_udp_cb = nullptr;
void* g_udp_ctx = nullptr;
uint16_t g_ip_ident = 1u;

void zero_bytes(void* dst, size_t count) {
    auto* d = static_cast<uint8_t*>(dst);
    for (size_t i = 0; i < count; ++i) {
        d[i] = 0;
    }
}

void copy_bytes(void* dst, const void* src, size_t count) {
    auto* d = static_cast<uint8_t*>(dst);
    const auto* s = static_cast<const uint8_t*>(src);
    for (size_t i = 0; i < count; ++i) {
        d[i] = s[i];
    }
}

bool mac_equal(MacAddress a, MacAddress b) {
    for (size_t i = 0; i < kMacAddrLen; ++i) {
        if (a.bytes[i] != b.bytes[i]) {
            return false;
        }
    }
    return true;
}

bool ip_equal(Ipv4Address a, Ipv4Address b) {
    for (size_t i = 0; i < kIpv4AddrLen; ++i) {
        if (a.bytes[i] != b.bytes[i]) {
            return false;
        }
    }
    return true;
}

bool ip_is_zero(Ipv4Address ip) {
    for (size_t i = 0; i < kIpv4AddrLen; ++i) {
        if (ip.bytes[i] != 0) {
            return false;
        }
    }
    return true;
}

bool same_subnet(Ipv4Address a, Ipv4Address b, Ipv4Address mask) {
    for (size_t i = 0; i < kIpv4AddrLen; ++i) {
        if ((a.bytes[i] & mask.bytes[i]) != (b.bytes[i] & mask.bytes[i])) {
            return false;
        }
    }
    return true;
}

void write_be16(uint8_t* dst, uint16_t val) {
    const uint16_t be = htons(val);
    copy_bytes(dst, &be, sizeof(be));
}

uint16_t read_be16(const uint8_t* src) {
    uint16_t be = 0;
    copy_bytes(&be, src, sizeof(be));
    return ntohs(be);
}

void write_be32(uint8_t* dst, uint32_t val) {
    const uint32_t be = htonl(val);
    copy_bytes(dst, &be, sizeof(be));
}

uint32_t read_be32(const uint8_t* src) {
    uint32_t be = 0;
    copy_bytes(&be, src, sizeof(be));
    return ntohl(be);
}

void arp_insert_or_update(Ipv4Address ip, MacAddress mac) {
    if (ip_is_zero(ip) || mac_equal(mac, kZeroMac)) {
        return;
    }

    for (size_t i = 0; i < kArpTableSize; ++i) {
        if (g_arp_table[i].valid && ip_equal(g_arp_table[i].ip, ip)) {
            g_arp_table[i].mac = mac;
            return;
        }
    }

    for (size_t i = 0; i < kArpTableSize; ++i) {
        if (!g_arp_table[i].valid) {
            g_arp_table[i].ip = ip;
            g_arp_table[i].mac = mac;
            g_arp_table[i].valid = true;
            return;
        }
    }

    g_arp_table[g_arp_next_slot].ip = ip;
    g_arp_table[g_arp_next_slot].mac = mac;
    g_arp_table[g_arp_next_slot].valid = true;
    g_arp_next_slot = (g_arp_next_slot + 1) % kArpTableSize;
}

Ipv4Address next_hop_for(Ipv4Address dst_ip) {
    if (ip_is_zero(g_config.netmask) || ip_is_zero(g_config.gateway)) {
        return dst_ip;
    }
    if (same_subnet(g_config.ip, dst_ip, g_config.netmask)) {
        return dst_ip;
    }
    return g_config.gateway;
}

Status transmit_raw_frame(const uint8_t* frame, size_t len) {
    if (!g_initialized) {
        return Status::NotInitialized;
    }
    if (!g_config.link_up) {
        return Status::LinkDown;
    }
    if (frame == nullptr || len == 0 || len > kMaxFrameBytes) {
        return Status::InvalidArg;
    }
    if (g_ops.transmit == nullptr) {
        return Status::TransmitFailed;
    }
    if (!g_ops.transmit(g_ops.ctx, frame, len)) {
        return Status::TransmitFailed;
    }
    return Status::Ok;
}

void write_ethernet_header(uint8_t* dst, MacAddress dst_mac, MacAddress src_mac, uint16_t ethertype) {
    copy_bytes(dst, dst_mac.bytes, kMacAddrLen);
    copy_bytes(dst + kMacAddrLen, src_mac.bytes, kMacAddrLen);
    write_be16(dst + 12, ethertype);
}

Status send_arp_packet(uint16_t opcode, MacAddress target_mac, Ipv4Address target_ip) {
    if (!g_initialized) {
        return Status::NotInitialized;
    }
    if (!g_config.link_up) {
        return Status::LinkDown;
    }

    const size_t total_len = kEthernetHeaderLen + kArpFramePayloadLen;
    if (total_len > kMaxFrameBytes) {
        return Status::BufferTooSmall;
    }

    uint8_t frame[kMaxFrameBytes];
    zero_bytes(frame, sizeof(frame));

    const MacAddress eth_dst = (opcode == kArpOpRequest) ? kBroadcastMac : target_mac;
    write_ethernet_header(frame, eth_dst, g_config.mac, kEtherTypeArp);

    uint8_t* arp = frame + kEthernetHeaderLen;
    write_be16(arp + 0, kArpHtypeEthernet);
    write_be16(arp + 2, kArpPtypeIpv4);
    arp[4] = static_cast<uint8_t>(kMacAddrLen);
    arp[5] = static_cast<uint8_t>(kIpv4AddrLen);
    write_be16(arp + 6, opcode);
    copy_bytes(arp + 8, g_config.mac.bytes, kMacAddrLen);
    copy_bytes(arp + 14, g_config.ip.bytes, kIpv4AddrLen);
    copy_bytes(arp + 18, target_mac.bytes, kMacAddrLen);
    copy_bytes(arp + 24, target_ip.bytes, kIpv4AddrLen);

    return transmit_raw_frame(frame, total_len);
}

Status send_ipv4_packet(Ipv4Address dst_ip,
                        uint8_t protocol,
                        const uint8_t* payload,
                        size_t payload_len) {
    if (!g_initialized) {
        return Status::NotInitialized;
    }
    if (!g_config.link_up) {
        return Status::LinkDown;
    }
    if (payload == nullptr && payload_len > 0) {
        return Status::InvalidArg;
    }
    if (payload_len > kMaxFrameBytes - kEthernetHeaderLen - kIpv4MinHeaderLen) {
        return Status::BufferTooSmall;
    }

    const Ipv4Address nh = next_hop_for(dst_ip);
    MacAddress dst_mac{};
    if (!arp_lookup(nh, &dst_mac)) {
        send_arp_request(nh);
        return Status::ArpPending;
    }

    const size_t ip_total_len = kIpv4MinHeaderLen + payload_len;
    const size_t frame_len = kEthernetHeaderLen + ip_total_len;
    if (frame_len > kMaxFrameBytes) {
        return Status::BufferTooSmall;
    }

    uint8_t frame[kMaxFrameBytes];
    zero_bytes(frame, sizeof(frame));

    write_ethernet_header(frame, dst_mac, g_config.mac, kEtherTypeIpv4);

    uint8_t* ip = frame + kEthernetHeaderLen;
    ip[0] = static_cast<uint8_t>((kIpv4Version4 << 4) | 5u); // Version 4, IHL 5 (20 bytes)
    ip[1] = 0u;                                              // DSCP / ECN
    write_be16(ip + 2, static_cast<uint16_t>(ip_total_len));
    write_be16(ip + 4, g_ip_ident++);
    write_be16(ip + 6, 0u);                                  // Flags / Fragment offset
    ip[8] = kIpv4DefaultTtl;
    ip[9] = protocol;
    write_be16(ip + 10, 0u);                                 // Checksum placeholder
    copy_bytes(ip + 12, g_config.ip.bytes, kIpv4AddrLen);
    copy_bytes(ip + 16, dst_ip.bytes, kIpv4AddrLen);

    const uint16_t hdr_csum = internet_checksum(ip, kIpv4MinHeaderLen);
    write_be16(ip + 10, hdr_csum);

    if (payload_len > 0) {
        copy_bytes(ip + kIpv4MinHeaderLen, payload, payload_len);
    }

    return transmit_raw_frame(frame, frame_len);
}

void handle_arp_frame(const uint8_t* arp, size_t len) {
    if (arp == nullptr || len < kArpFramePayloadLen) {
        return;
    }

    const uint16_t htype = read_be16(arp + 0);
    const uint16_t ptype = read_be16(arp + 2);
    const uint8_t hlen = arp[4];
    const uint8_t plen = arp[5];
    const uint16_t oper = read_be16(arp + 6);

    if (htype != kArpHtypeEthernet || ptype != kArpPtypeIpv4 ||
        hlen != kMacAddrLen || plen != kIpv4AddrLen) {
        return;
    }

    MacAddress sender_mac{};
    Ipv4Address sender_ip{};
    Ipv4Address target_ip{};
    copy_bytes(sender_mac.bytes, arp + 8, kMacAddrLen);
    copy_bytes(sender_ip.bytes, arp + 14, kIpv4AddrLen);
    copy_bytes(target_ip.bytes, arp + 24, kIpv4AddrLen);

    arp_insert_or_update(sender_ip, sender_mac);

    if (oper == kArpOpRequest && !ip_is_zero(g_config.ip) && ip_equal(target_ip, g_config.ip)) {
        send_arp_packet(kArpOpReply, sender_mac, sender_ip);
    }
}

void handle_icmp_frame(Ipv4Address src_ip, const uint8_t* icmp, size_t len) {
    if (icmp == nullptr || len < kIcmpMinHeaderLen) {
        return;
    }

    const uint8_t type = icmp[0];
    const uint8_t code = icmp[1];
    const uint16_t received_csum = read_be16(icmp + 2);

    uint8_t verify_buf[kMaxFrameBytes];
    if (len > sizeof(verify_buf)) {
        return;
    }
    copy_bytes(verify_buf, icmp, len);
    verify_buf[2] = 0;
    verify_buf[3] = 0;
    if (internet_checksum(verify_buf, len) != received_csum) {
        return;
    }

    if (type == kIcmpEchoRequest && code == 0) {
        const uint16_t ident = read_be16(icmp + 4);
        const uint16_t seq = read_be16(icmp + 6);
        const uint8_t* payload = icmp + kIcmpMinHeaderLen;
        const size_t payload_len = len - kIcmpMinHeaderLen;
        send_icmp_echo_reply(src_ip, ident, seq, payload, payload_len);
    }
}

void handle_udp_frame(Ipv4Address src_ip, const uint8_t* udp, size_t len) {
    if (udp == nullptr || len < kUdpHeaderLen) {
        return;
    }

    const uint16_t src_port = read_be16(udp + 0);
    const uint16_t dst_port = read_be16(udp + 2);
    const uint16_t udp_len = read_be16(udp + 4);

    if (udp_len < kUdpHeaderLen || static_cast<size_t>(udp_len) > len) {
        return;
    }

    const uint8_t* payload = udp + kUdpHeaderLen;
    const size_t payload_len = static_cast<size_t>(udp_len) - kUdpHeaderLen;

    if (g_udp_cb != nullptr) {
        g_udp_cb(g_udp_ctx, src_ip, src_port, dst_port, payload, payload_len);
    }
}

void handle_ipv4_frame(const uint8_t* ip, size_t len) {
    if (ip == nullptr || len < kIpv4MinHeaderLen) {
        return;
    }

    const uint8_t version = static_cast<uint8_t>(ip[0] >> 4);
    const size_t ihl_bytes = static_cast<size_t>(ip[0] & 0x0Fu) * 4u;
    if (version != kIpv4Version4 || ihl_bytes < kIpv4MinHeaderLen || ihl_bytes > len) {
        return;
    }

    const uint16_t total_len = read_be16(ip + 2);
    if (total_len < ihl_bytes || static_cast<size_t>(total_len) > len) {
        return;
    }

    const uint16_t hdr_csum = read_be16(ip + 10);
    uint8_t hdr_copy[60]; // Max IHL is 15 * 4 = 60 bytes
    copy_bytes(hdr_copy, ip, ihl_bytes);
    hdr_copy[10] = 0;
    hdr_copy[11] = 0;
    if (internet_checksum(hdr_copy, ihl_bytes) != hdr_csum) {
        return;
    }

    Ipv4Address src_ip{};
    Ipv4Address dst_ip{};
    copy_bytes(src_ip.bytes, ip + 12, kIpv4AddrLen);
    copy_bytes(dst_ip.bytes, ip + 16, kIpv4AddrLen);

    if (!ip_is_zero(g_config.ip) && !ip_equal(dst_ip, g_config.ip) &&
        dst_ip.bytes[3] != 255u) {
        return;
    }

    const uint8_t proto = ip[9];
    const uint8_t* l4_data = ip + ihl_bytes;
    const size_t l4_len = static_cast<size_t>(total_len) - ihl_bytes;

    if (proto == kIpProtoIcmp) {
        handle_icmp_frame(src_ip, l4_data, l4_len);
    } else if (proto == kIpProtoUdp) {
        handle_udp_frame(src_ip, l4_data, l4_len);
    }
}

void process_ethernet_frame(const uint8_t* frame, size_t len) {
    if (frame == nullptr || len < kEthernetHeaderLen) {
        return;
    }

    MacAddress dst_mac{};
    MacAddress src_mac{};
    copy_bytes(dst_mac.bytes, frame, kMacAddrLen);
    copy_bytes(src_mac.bytes, frame + kMacAddrLen, kMacAddrLen);
    const uint16_t ethertype = read_be16(frame + 12);

    if (!mac_equal(dst_mac, g_config.mac) && !mac_equal(dst_mac, kBroadcastMac)) {
        return;
    }

    const uint8_t* payload = frame + kEthernetHeaderLen;
    const size_t payload_len = len - kEthernetHeaderLen;

    if (ethertype == kEtherTypeArp) {
        handle_arp_frame(payload, payload_len);
    } else if (ethertype == kEtherTypeIpv4) {
        handle_ipv4_frame(payload, payload_len);
    }
}

} // namespace

Status init(const NetConfig& config, const DriverOps& ops) {
    if (mac_equal(config.mac, kZeroMac)) {
        return Status::InvalidArg;
    }

    g_config = config;
    g_ops = ops;
    g_arp_next_slot = 0;
    g_ip_ident = 1u;
    g_udp_cb = nullptr;
    g_udp_ctx = nullptr;

    for (size_t i = 0; i < kMaxPacketPool; ++i) {
        zero_bytes(&g_packet_pool[i], sizeof(g_packet_pool[i]));
        g_packet_pool[i].in_use = false;
    }

    for (size_t i = 0; i < kArpTableSize; ++i) {
        zero_bytes(&g_arp_table[i], sizeof(g_arp_table[i]));
        g_arp_table[i].valid = false;
    }

    g_initialized = true;
    return Status::Ok;
}

bool is_initialized() {
    return g_initialized;
}

void set_link_up(bool up) {
    g_config.link_up = up;
}

const NetConfig& config() {
    return g_config;
}

Packet* alloc_packet() {
    for (size_t i = 0; i < kMaxPacketPool; ++i) {
        if (!g_packet_pool[i].in_use) {
            zero_bytes(&g_packet_pool[i], sizeof(g_packet_pool[i]));
            g_packet_pool[i].in_use = true;
            return &g_packet_pool[i];
        }
    }
    return nullptr;
}

void free_packet(Packet* pkt) {
    if (pkt == nullptr) {
        return;
    }
    for (size_t i = 0; i < kMaxPacketPool; ++i) {
        if (&g_packet_pool[i] == pkt) {
            zero_bytes(pkt, sizeof(*pkt));
            pkt->in_use = false;
            return;
        }
    }
}

size_t free_packet_count() {
    size_t count = 0;
    for (size_t i = 0; i < kMaxPacketPool; ++i) {
        if (!g_packet_pool[i].in_use) {
            ++count;
        }
    }
    return count;
}

void poll() {
    if (!g_initialized || !g_config.link_up || g_ops.poll_receive == nullptr) {
        return;
    }

    Packet* pkt = alloc_packet();
    if (pkt == nullptr) {
        return;
    }

    while (true) {
        const size_t rx_len = g_ops.poll_receive(g_ops.ctx, pkt->data, kMaxFrameBytes);
        if (rx_len == 0) {
            break;
        }
        pkt->length = (rx_len > kMaxFrameBytes) ? kMaxFrameBytes : rx_len;
        process_ethernet_frame(pkt->data, pkt->length);
        zero_bytes(pkt->data, pkt->length);
        pkt->length = 0;
    }

    free_packet(pkt);
}

bool arp_lookup(Ipv4Address ip, MacAddress* out_mac) {
    if (out_mac == nullptr || ip_is_zero(ip)) {
        return false;
    }
    for (size_t i = 0; i < kArpTableSize; ++i) {
        if (g_arp_table[i].valid && ip_equal(g_arp_table[i].ip, ip)) {
            *out_mac = g_arp_table[i].mac;
            return true;
        }
    }
    return false;
}

Status send_arp_request(Ipv4Address target_ip) {
    if (!g_initialized) {
        return Status::NotInitialized;
    }
    if (ip_is_zero(target_ip)) {
        return Status::InvalidArg;
    }
    return send_arp_packet(kArpOpRequest, kZeroMac, target_ip);
}

const ArpEntry* arp_table(size_t* out_count) {
    if (out_count != nullptr) {
        *out_count = kArpTableSize;
    }
    return g_arp_table;
}

Status send_icmp_echo_reply(Ipv4Address dst_ip,
                            uint16_t identifier,
                            uint16_t sequence,
                            const uint8_t* payload,
                            size_t len) {
    if (!g_initialized) {
        return Status::NotInitialized;
    }
    if (payload == nullptr && len > 0) {
        return Status::InvalidArg;
    }

    const size_t icmp_len = kIcmpMinHeaderLen + len;
    const size_t max_icmp = kMaxFrameBytes - kEthernetHeaderLen - kIpv4MinHeaderLen;
    if (icmp_len > max_icmp) {
        return Status::BufferTooSmall;
    }

    uint8_t icmp_buf[kMaxFrameBytes - kEthernetHeaderLen - kIpv4MinHeaderLen];
    zero_bytes(icmp_buf, sizeof(icmp_buf));

    icmp_buf[0] = kIcmpEchoReply;
    icmp_buf[1] = 0u;
    write_be16(icmp_buf + 2, 0u);
    write_be16(icmp_buf + 4, identifier);
    write_be16(icmp_buf + 6, sequence);

    if (len > 0) {
        copy_bytes(icmp_buf + kIcmpMinHeaderLen, payload, len);
    }

    const uint16_t csum = internet_checksum(icmp_buf, icmp_len);
    write_be16(icmp_buf + 2, csum);

    return send_ipv4_packet(dst_ip, kIpProtoIcmp, icmp_buf, icmp_len);
}

Status send_udp(Ipv4Address dst_ip,
                uint16_t src_port,
                uint16_t dst_port,
                const uint8_t* payload,
                size_t len) {
    if (!g_initialized) {
        return Status::NotInitialized;
    }
    if (payload == nullptr && len > 0) {
        return Status::InvalidArg;
    }

    const size_t udp_len = kUdpHeaderLen + len;
    const size_t max_udp = kMaxFrameBytes - kEthernetHeaderLen - kIpv4MinHeaderLen;
    if (udp_len > max_udp || udp_len > 0xFFFFu) {
        return Status::BufferTooSmall;
    }

    uint8_t udp_buf[kMaxFrameBytes - kEthernetHeaderLen - kIpv4MinHeaderLen];
    zero_bytes(udp_buf, sizeof(udp_buf));

    write_be16(udp_buf + 0, src_port);
    write_be16(udp_buf + 2, dst_port);
    write_be16(udp_buf + 4, static_cast<uint16_t>(udp_len));
    write_be16(udp_buf + 6, 0u); // Optional UDP checksum (0 = unused in IPv4)

    if (len > 0) {
        copy_bytes(udp_buf + kUdpHeaderLen, payload, len);
    }

    return send_ipv4_packet(dst_ip, kIpProtoUdp, udp_buf, udp_len);
}

void set_udp_callback(UdpRecvCallback cb, void* user_ctx) {
    g_udp_cb = cb;
    g_udp_ctx = user_ctx;
}

uint16_t htons(uint16_t val) {
    return static_cast<uint16_t>((val >> 8) | (val << 8));
}

uint16_t ntohs(uint16_t val) {
    return htons(val);
}

uint32_t htonl(uint32_t val) {
    return ((val & 0x000000FFu) << 24) |
           ((val & 0x0000FF00u) << 8)  |
           ((val & 0x00FF0000u) >> 8)  |
           ((val & 0xFF000000u) >> 24);
}

uint32_t ntohl(uint32_t val) {
    return htonl(val);
}

uint16_t internet_checksum(const uint8_t* data, size_t len) {
    if (data == nullptr || len == 0) {
        return 0u;
    }

    uint32_t sum = 0u;
    size_t i = 0;

    while (i + 1 < len) {
        const uint16_t word = static_cast<uint16_t>((static_cast<uint16_t>(data[i]) << 8) |
                                                    static_cast<uint16_t>(data[i + 1]));
        sum += word;
        i += 2;
    }

    if (i < len) {
        const uint16_t tail = static_cast<uint16_t>(static_cast<uint16_t>(data[i]) << 8);
        sum += tail;
    }

    while ((sum >> 16) != 0u) {
        sum = (sum & 0xFFFFu) + (sum >> 16);
    }

    return static_cast<uint16_t>(~sum);
}

const char* status_to_string(Status status) {
    switch (status) {
        case Status::Ok:             return "Ok";
        case Status::InvalidArg:     return "InvalidArg";
        case Status::NotInitialized: return "NotInitialized";
        case Status::NoBuffer:       return "NoBuffer";
        case Status::LinkDown:       return "LinkDown";
        case Status::ArpPending:     return "ArpPending";
        case Status::Dropped:        return "Dropped";
        case Status::BufferTooSmall: return "BufferTooSmall";
        case Status::TransmitFailed: return "TransmitFailed";
        default:                     return "UnknownStatus";
    }
}

} // namespace tristos::net
