#include "e1000.hpp"

namespace tristos::drivers::e1000 {
namespace {

// Intel 8254x / e1000 MMIO register offsets
inline constexpr uint32_t kRegCtrl     = 0x0000u; // Device Control
inline constexpr uint32_t kRegStatus   = 0x0008u; // Device Status
inline constexpr uint32_t kRegEecd     = 0x0010u; // EEPROM/Flash Control
inline constexpr uint32_t kRegEerd     = 0x0014u; // EEPROM Read
inline constexpr uint32_t kRegCtrlExt  = 0x0018u; // Extended Device Control
inline constexpr uint32_t kRegIcr      = 0x00C0u; // Interrupt Cause Read
inline constexpr uint32_t kRegImc      = 0x00D8u; // Interrupt Mask Clear
inline constexpr uint32_t kRegRctl     = 0x0100u; // Receive Control
inline constexpr uint32_t kRegTctl     = 0x0400u; // Transmit Control
inline constexpr uint32_t kRegTipg     = 0x0410u; // Transmit IPG
inline constexpr uint32_t kRegRdbal    = 0x2800u; // RX Descriptor Base Low
inline constexpr uint32_t kRegRdbah    = 0x2804u; // RX Descriptor Base High
inline constexpr uint32_t kRegRdlen    = 0x2808u; // RX Descriptor Length
inline constexpr uint32_t kRegRdh      = 0x2810u; // RX Descriptor Head
inline constexpr uint32_t kRegRdt      = 0x2818u; // RX Descriptor Tail
inline constexpr uint32_t kRegTdbal    = 0x3800u; // TX Descriptor Base Low
inline constexpr uint32_t kRegTdbah    = 0x3804u; // TX Descriptor Base High
inline constexpr uint32_t kRegTdlen    = 0x3808u; // TX Descriptor Length
inline constexpr uint32_t kRegTdh      = 0x3810u; // TX Descriptor Head
inline constexpr uint32_t kRegTdt      = 0x3818u; // TX Descriptor Tail
inline constexpr uint32_t kRegMtaBase  = 0x5200u; // Multicast Table Array (128 dwords)
inline constexpr uint32_t kRegRal0     = 0x5400u; // Receive Address Low 0
inline constexpr uint32_t kRegRah0     = 0x5404u; // Receive Address High 0

// Control & Status bits
inline constexpr uint32_t kCtrlSlu     = 1u << 6;  // Set Link Up
inline constexpr uint32_t kCtrlRst     = 1u << 26; // Device Reset
inline constexpr uint32_t kStatusLu    = 1u << 1;  // Link Up

// Receive Control (RCTL) bits
inline constexpr uint32_t kRctlEn      = 1u << 1;  // Receiver Enable
inline constexpr uint32_t kRctlSbp     = 1u << 2;  // Store Bad Packets
inline constexpr uint32_t kRctlUpe     = 1u << 3;  // Unicast Promiscuous Enable
inline constexpr uint32_t kRctlMpe     = 1u << 4;  // Multicast Promiscuous Enable
inline constexpr uint32_t kRctlBam     = 1u << 15; // Broadcast Accept Mode
inline constexpr uint32_t kRctlBsize2k = 0u << 16; // 2048-byte RX buffer size
inline constexpr uint32_t kRctlSecrc   = 1u << 26; // Strip Ethernet CRC

// Transmit Control (TCTL) bits
inline constexpr uint32_t kTctlEn      = 1u << 1;  // Transmitter Enable
inline constexpr uint32_t kTctlPsp     = 1u << 3;  // Pad Short Packets
inline constexpr uint32_t kTctlCtShift = 4u;       // Collision Threshold shift
inline constexpr uint32_t kTctlColdShf = 12u;      // Collision Distance shift
inline constexpr uint32_t kTctlDefCt   = 15u;
inline constexpr uint32_t kTctlDefCold = 64u;

// Legacy TX descriptor command & status bits
inline constexpr uint8_t kTxCmdEop     = 1u << 0;  // End of Packet
inline constexpr uint8_t kTxCmdIfcs    = 1u << 1;  // Insert FCS/CRC
inline constexpr uint8_t kTxCmdRs      = 1u << 3;  // Report Status
inline constexpr uint8_t kTxStaDd      = 1u << 0;  // Descriptor Done

// Legacy RX descriptor status bits
inline constexpr uint8_t kRxStaDd      = 1u << 0;  // Descriptor Done
inline constexpr uint8_t kRxStaEop     = 1u << 1;  // End of Packet

struct RxDescriptor {
    uint64_t addr;
    uint16_t length;
    uint16_t checksum;
    uint8_t status;
    uint8_t errors;
    uint16_t special;
} __attribute__((packed, aligned(16)));

struct TxDescriptor {
    uint64_t addr;
    uint16_t length;
    uint8_t cso;
    uint8_t cmd;
    uint8_t status;
    uint8_t css;
    uint16_t special;
} __attribute__((packed, aligned(16)));

static_assert(sizeof(RxDescriptor) == 16, "e1000 RX descriptor must be 16 bytes");
static_assert(sizeof(TxDescriptor) == 16, "e1000 TX descriptor must be 16 bytes");

struct DriverState {
    volatile uint8_t* mmio;
    uint64_t hhdm_offset;
    tristos::net::MacAddress mac;
    bool link_up;
    bool initialized;
    size_t rx_idx;
    size_t tx_idx;
    RxDescriptor rx_ring[kRxRingSize] __attribute__((aligned(4096)));
    TxDescriptor tx_ring[kTxRingSize] __attribute__((aligned(4096)));
    uint8_t rx_buffers[kRxRingSize][kRxBufferBytes] __attribute__((aligned(4096)));
    uint8_t tx_buffers[kTxRingSize][kTxBufferBytes] __attribute__((aligned(4096)));
};

DriverState g_state{};

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

void delay_cycles(uint32_t spins) {
    for (uint32_t i = 0; i < spins; ++i) {
        asm volatile("pause");
    }
}

uint32_t mmio_read32(uint32_t reg) {
    auto* ptr = reinterpret_cast<volatile const uint32_t*>(g_state.mmio + reg);
    return *ptr;
}

void mmio_write32(uint32_t reg, uint32_t value) {
    auto* ptr = reinterpret_cast<volatile uint32_t*>(g_state.mmio + reg);
    *ptr = value;
}

uint64_t virt_to_phys(const void* virt_addr) {
    const uint64_t va = reinterpret_cast<uintptr_t>(virt_addr);
    if (g_state.hhdm_offset != 0 && va >= g_state.hhdm_offset) {
        return va - g_state.hhdm_offset;
    }
    return va;
}

uint16_t read_eeprom_word(uint8_t addr) {
    mmio_write32(kRegEerd, (static_cast<uint32_t>(addr) << 8) | 1u);
    for (uint32_t i = 0; i < 100000u; ++i) {
        const uint32_t val = mmio_read32(kRegEerd);
        if ((val & (1u << 4)) != 0) {
            return static_cast<uint16_t>((val >> 16) & 0xFFFFu);
        }
        delay_cycles(16);
    }
    return 0u;
}

bool read_mac_from_ral_rah(tristos::net::MacAddress* out_mac) {
    const uint32_t ral = mmio_read32(kRegRal0);
    const uint32_t rah = mmio_read32(kRegRah0);

    out_mac->bytes[0] = static_cast<uint8_t>(ral & 0xFFu);
    out_mac->bytes[1] = static_cast<uint8_t>((ral >> 8) & 0xFFu);
    out_mac->bytes[2] = static_cast<uint8_t>((ral >> 16) & 0xFFu);
    out_mac->bytes[3] = static_cast<uint8_t>((ral >> 24) & 0xFFu);
    out_mac->bytes[4] = static_cast<uint8_t>(rah & 0xFFu);
    out_mac->bytes[5] = static_cast<uint8_t>((rah >> 8) & 0xFFu);

    for (size_t i = 0; i < tristos::net::kMacAddrLen; ++i) {
        if (out_mac->bytes[i] != 0u) {
            return true;
        }
    }
    return false;
}

void load_mac_address() {
    tristos::net::MacAddress mac{};
    if (!read_mac_from_ral_rah(&mac)) {
        const uint16_t w0 = read_eeprom_word(0u);
        const uint16_t w1 = read_eeprom_word(1u);
        const uint16_t w2 = read_eeprom_word(2u);
        mac.bytes[0] = static_cast<uint8_t>(w0 & 0xFFu);
        mac.bytes[1] = static_cast<uint8_t>((w0 >> 8) & 0xFFu);
        mac.bytes[2] = static_cast<uint8_t>(w1 & 0xFFu);
        mac.bytes[3] = static_cast<uint8_t>((w1 >> 8) & 0xFFu);
        mac.bytes[4] = static_cast<uint8_t>(w2 & 0xFFu);
        mac.bytes[5] = static_cast<uint8_t>((w2 >> 8) & 0xFFu);
    }

    g_state.mac = mac;

    const uint32_t ral =
        static_cast<uint32_t>(mac.bytes[0]) |
        (static_cast<uint32_t>(mac.bytes[1]) << 8) |
        (static_cast<uint32_t>(mac.bytes[2]) << 16) |
        (static_cast<uint32_t>(mac.bytes[3]) << 24);
    const uint32_t rah =
        static_cast<uint32_t>(mac.bytes[4]) |
        (static_cast<uint32_t>(mac.bytes[5]) << 8) |
        (1u << 31); // Address Valid (AV)

    mmio_write32(kRegRal0, ral);
    mmio_write32(kRegRah0, rah);
}

void reset_controller() {
    mmio_write32(kRegImc, 0xFFFFFFFFu);
    (void)mmio_read32(kRegIcr);

    const uint32_t ctrl = mmio_read32(kRegCtrl);
    mmio_write32(kRegCtrl, ctrl | kCtrlRst);

    for (uint32_t i = 0; i < 100000u; ++i) {
        if ((mmio_read32(kRegCtrl) & kCtrlRst) == 0u) {
            break;
        }
        delay_cycles(16);
    }

    mmio_write32(kRegImc, 0xFFFFFFFFu);
    (void)mmio_read32(kRegIcr);

    const uint32_t post_ctrl = mmio_read32(kRegCtrl);
    mmio_write32(kRegCtrl, post_ctrl | kCtrlSlu);
}

void init_rx_ring() {
    zero_bytes(g_state.rx_ring, sizeof(g_state.rx_ring));
    zero_bytes(g_state.rx_buffers, sizeof(g_state.rx_buffers));

    for (size_t i = 0; i < kRxRingSize; ++i) {
        g_state.rx_ring[i].addr = virt_to_phys(g_state.rx_buffers[i]);
        g_state.rx_ring[i].status = 0u;
    }

    const uint64_t ring_phys = virt_to_phys(g_state.rx_ring);
    mmio_write32(kRegRdbal, static_cast<uint32_t>(ring_phys & 0xFFFFFFFFu));
    mmio_write32(kRegRdbah, static_cast<uint32_t>((ring_phys >> 32) & 0xFFFFFFFFu));
    mmio_write32(kRegRdlen, static_cast<uint32_t>(kRxRingSize * sizeof(RxDescriptor)));
    mmio_write32(kRegRdh, 0u);
    mmio_write32(kRegRdt, static_cast<uint32_t>(kRxRingSize - 1u));
    g_state.rx_idx = 0u;

    for (size_t i = 0; i < 128u; ++i) {
        mmio_write32(kRegMtaBase + static_cast<uint32_t>(i * 4u), 0u);
    }

    const uint32_t rctl = kRctlEn | kRctlBam | kRctlBsize2k | kRctlSecrc;
    mmio_write32(kRegRctl, rctl);
}

void init_tx_ring() {
    zero_bytes(g_state.tx_ring, sizeof(g_state.tx_ring));
    zero_bytes(g_state.tx_buffers, sizeof(g_state.tx_buffers));

    for (size_t i = 0; i < kTxRingSize; ++i) {
        g_state.tx_ring[i].addr = virt_to_phys(g_state.tx_buffers[i]);
        g_state.tx_ring[i].cmd = 0u;
        g_state.tx_ring[i].status = kTxStaDd;
    }

    const uint64_t ring_phys = virt_to_phys(g_state.tx_ring);
    mmio_write32(kRegTdbal, static_cast<uint32_t>(ring_phys & 0xFFFFFFFFu));
    mmio_write32(kRegTdbah, static_cast<uint32_t>((ring_phys >> 32) & 0xFFFFFFFFu));
    mmio_write32(kRegTdlen, static_cast<uint32_t>(kTxRingSize * sizeof(TxDescriptor)));
    mmio_write32(kRegTdh, 0u);
    mmio_write32(kRegTdt, 0u);
    g_state.tx_idx = 0u;

    mmio_write32(kRegTipg, 0x0060200Au); // Standard IEEE 802.3 IPG timing
    const uint32_t tctl =
        kTctlEn |
        kTctlPsp |
        (kTctlDefCt << kTctlCtShift) |
        (kTctlDefCold << kTctlColdShf);
    mmio_write32(kRegTctl, tctl);
}

} // namespace

Status init(const Config& config) {
    if (config.mmio_base == nullptr) {
        return Status::InvalidArg;
    }

    zero_bytes(&g_state, sizeof(g_state));
    g_state.mmio = config.mmio_base;
    g_state.hhdm_offset = config.hhdm_offset;

    reset_controller();
    load_mac_address();
    init_rx_ring();
    init_tx_ring();

    poll_link_status();
    g_state.initialized = true;

    if (config.auto_init_net) {
        tristos::net::NetConfig net_cfg{};
        net_cfg.mac = g_state.mac;
        net_cfg.ip = config.ip;
        net_cfg.netmask = config.netmask;
        net_cfg.gateway = config.gateway;
        net_cfg.link_up = g_state.link_up;
        (void)tristos::net::init(net_cfg, driver_ops());
    }

    return Status::Ok;
}

bool is_initialized() {
    return g_state.initialized;
}

void poll_link_status() {
    if (g_state.mmio == nullptr) {
        g_state.link_up = false;
        return;
    }
    const bool up = (mmio_read32(kRegStatus) & kStatusLu) != 0u;
    g_state.link_up = up;
    if (tristos::net::is_initialized()) {
        tristos::net::set_link_up(up);
    }
}

tristos::net::MacAddress mac_address() {
    return g_state.mac;
}

bool link_up() {
    return g_state.link_up;
}

tristos::net::DriverOps driver_ops() {
    tristos::net::DriverOps ops{};
    ops.ctx = &g_state;
    ops.transmit = transmit_frame;
    ops.poll_receive = poll_receive_frame;
    return ops;
}

bool transmit_frame(void* ctx, const uint8_t* frame, size_t len) {
    auto* st = static_cast<DriverState*>(ctx);
    if (st == nullptr || !st->initialized || frame == nullptr || len == 0u || len > kTxBufferBytes) {
        return false;
    }

    const size_t idx = st->tx_idx;
    TxDescriptor& desc = st->tx_ring[idx];

    for (uint32_t spin = 0; spin < 200000u; ++spin) {
        if ((desc.status & kTxStaDd) != 0u || desc.cmd == 0u) {
            break;
        }
        delay_cycles(8);
    }

    copy_bytes(st->tx_buffers[idx], frame, len);
    desc.addr = virt_to_phys(st->tx_buffers[idx]);
    desc.length = static_cast<uint16_t>(len);
    desc.cso = 0u;
    desc.css = 0u;
    desc.status = 0u;
    desc.cmd = kTxCmdEop | kTxCmdIfcs | kTxCmdRs;

    asm volatile("" : : "m"(desc) : "memory");

    st->tx_idx = (idx + 1u) % kTxRingSize;
    mmio_write32(kRegTdt, static_cast<uint32_t>(st->tx_idx));

    for (uint32_t spin = 0; spin < 200000u; ++spin) {
        if ((desc.status & kTxStaDd) != 0u) {
            return true;
        }
        delay_cycles(8);
    }

    return true;
}

size_t poll_receive_frame(void* ctx, uint8_t* out_frame, size_t max_len) {
    auto* st = static_cast<DriverState*>(ctx);
    if (st == nullptr || !st->initialized || out_frame == nullptr || max_len == 0u) {
        return 0u;
    }

    const size_t idx = st->rx_idx;
    RxDescriptor& desc = st->rx_ring[idx];

    if ((desc.status & kRxStaDd) == 0u) {
        return 0u;
    }

    size_t copy_len = static_cast<size_t>(desc.length);
    if (copy_len > max_len) {
        copy_len = max_len;
    }
    if (copy_len > kRxBufferBytes) {
        copy_len = kRxBufferBytes;
    }

    copy_bytes(out_frame, st->rx_buffers[idx], copy_len);

    desc.status = 0u;
    desc.errors = 0u;
    desc.length = 0u;
    desc.addr = virt_to_phys(st->rx_buffers[idx]);

    st->rx_idx = (idx + 1u) % kRxRingSize;
    mmio_write32(kRegRdt, static_cast<uint32_t>(idx));

    return copy_len;
}

const char* status_to_string(Status status) {
    switch (status) {
        case Status::Ok:                 return "Ok";
        case Status::InvalidArg:         return "InvalidArg";
        case Status::AlreadyInitialized: return "AlreadyInitialized";
        case Status::NotInitialized:     return "NotInitialized";
        case Status::IoSpaceUnsupported: return "IoSpaceUnsupported";
        case Status::LinkDown:           return "LinkDown";
        case Status::TxTimeout:          return "TxTimeout";
        default:                         return "UnknownStatus";
    }
}

} // namespace tristos::drivers::e1000
