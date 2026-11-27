#include "storage.hpp"

namespace tristos::storage {
namespace {

inline constexpr size_t kMaxBounceBlockSize = 4096;

struct DeviceSlot {
    BlockDevice* device = nullptr;
    uint32_t id = 0;
    bool in_use = false;
};

DeviceSlot g_devices[kMaxStorageDevices];
uint32_t g_next_device_id = 1;
bool g_initialized = false;

void copy_bytes(void* dst, const void* src, size_t count) {
    auto* d = static_cast<uint8_t*>(dst);
    const auto* s = static_cast<const uint8_t*>(src);
    for (size_t i = 0; i < count; ++i) {
        d[i] = s[i];
    }
}

void zero_bytes(void* dst, size_t count) {
    auto* d = static_cast<uint8_t*>(dst);
    for (size_t i = 0; i < count; ++i) {
        d[i] = 0;
    }
}

void copy_cstr(char* dst, size_t dst_cap, const char* src) {
    if (dst == nullptr || dst_cap == 0) {
        return;
    }
    if (src == nullptr) {
        dst[0] = '\0';
        return;
    }
    size_t i = 0;
    while (i + 1 < dst_cap && src[i] != '\0') {
        dst[i] = src[i];
        ++i;
    }
    dst[i] = '\0';
}

bool cstr_eq(const char* a, const char* b) {
    if (a == nullptr || b == nullptr) {
        return a == b;
    }
    size_t i = 0;
    while (a[i] != '\0' && b[i] != '\0') {
        if (a[i] != b[i]) {
            return false;
        }
        ++i;
    }
    return a[i] == b[i];
}

class RamDiskDevice final : public BlockDevice {
public:
    RamDiskDevice(const RamDiskConfig& config, uint32_t assigned_id)
        : buffer_(config.buffer),
          buffer_size_(config.buffer_size),
          owns_buffer_(false) {
        zero_bytes(&info_, sizeof(info_));
        info_.device_id = assigned_id;
        info_.block_size = (config.block_size != 0) ? config.block_size : kDefaultSectorSize;
        info_.block_count = (info_.block_size != 0) ? (buffer_size_ / info_.block_size) : 0;
        info_.flags = config.flags;
        info_.kind = has_flag(config.flags, DeviceFlags::BootDevice)
                         ? DeviceKind::InitrdImage
                         : DeviceKind::RamDisk;

        if (config.name != nullptr && config.name[0] != '\0') {
            copy_cstr(info_.name, sizeof(info_.name), config.name);
        } else {
            copy_cstr(info_.name, sizeof(info_.name), "ramdisk");
        }
    }

    const DeviceInfo& info() const override {
        return info_;
    }

    Status read_blocks(uint64_t lba, uint32_t count, void* out_buf) override {
        if (out_buf == nullptr || buffer_ == nullptr || info_.block_size == 0) {
            return Status::InvalidArg;
        }
        if (count == 0) {
            return Status::Ok;
        }
        if (lba >= info_.block_count || count > info_.block_count - lba) {
            return Status::OutOfRange;
        }

        const uint64_t byte_offset = lba * static_cast<uint64_t>(info_.block_size);
        const size_t byte_count = static_cast<size_t>(count) * static_cast<size_t>(info_.block_size);
        copy_bytes(out_buf, buffer_ + byte_offset, byte_count);
        return Status::Ok;
    }

    Status write_blocks(uint64_t lba, uint32_t count, const void* in_buf) override {
        if (in_buf == nullptr || buffer_ == nullptr || info_.block_size == 0) {
            return Status::InvalidArg;
        }
        if (info_.is_read_only()) {
            return Status::ReadOnly;
        }
        if (count == 0) {
            return Status::Ok;
        }
        if (lba >= info_.block_count || count > info_.block_count - lba) {
            return Status::OutOfRange;
        }

        const uint64_t byte_offset = lba * static_cast<uint64_t>(info_.block_size);
        const size_t byte_count = static_cast<size_t>(count) * static_cast<size_t>(info_.block_size);
        copy_bytes(buffer_ + byte_offset, in_buf, byte_count);
        return Status::Ok;
    }

    Status flush() override {
        return Status::Ok;
    }

private:
    DeviceInfo info_;
    uint8_t* buffer_;
    size_t buffer_size_;
    bool owns_buffer_;
};

inline constexpr size_t kMaxRamDiskInstances = 8;
struct RamDiskSlot {
    RamDiskDevice instance;
    bool used;
};

// Static pool avoids requiring C++ operator new/delete before kernel heap is active
alignas(RamDiskDevice) uint8_t g_ramdisk_pool_storage[sizeof(RamDiskDevice) * kMaxRamDiskInstances];
bool g_ramdisk_used[kMaxRamDiskInstances];

RamDiskDevice* alloc_ramdisk_slot(const RamDiskConfig& config, uint32_t id) {
    auto* slots = reinterpret_cast<RamDiskDevice*>(g_ramdisk_pool_storage);
    for (size_t i = 0; i < kMaxRamDiskInstances; ++i) {
        if (!g_ramdisk_used[i]) {
            g_ramdisk_used[i] = true;
            return new (&slots[i]) RamDiskDevice(config, id);
        }
    }
    return nullptr;
}

bool is_pooled_ramdisk(const BlockDevice* dev, size_t* out_index) {
    if (dev == nullptr) {
        return false;
    }
    auto* slots = reinterpret_cast<RamDiskDevice*>(g_ramdisk_pool_storage);
    for (size_t i = 0; i < kMaxRamDiskInstances; ++i) {
        if (g_ramdisk_used[i] && &slots[i] == dev) {
            if (out_index != nullptr) {
                *out_index = i;
            }
            return true;
        }
    }
    return false;
}

} // namespace

Status BlockDevice::read_bytes(uint64_t offset, size_t size, void* out_buf) {
    if (size == 0) {
        return Status::Ok;
    }
    if (out_buf == nullptr) {
        return Status::InvalidArg;
    }

    const DeviceInfo& dev_info = info();
    const uint32_t block_sz = dev_info.block_size;
    if (block_sz == 0 || block_sz > kMaxBounceBlockSize) {
        return Status::Unsupported;
    }

    const uint64_t total = dev_info.total_bytes();
    if (offset >= total || static_cast<uint64_t>(size) > total - offset) {
        return Status::OutOfRange;
    }

    auto* dst = static_cast<uint8_t*>(out_buf);
    size_t remaining = size;
    uint64_t cur_offset = offset;
    uint8_t bounce[kMaxBounceBlockSize];

    // Leading partial block
    const uint32_t lead_off = static_cast<uint32_t>(cur_offset % block_sz);
    if (lead_off != 0) {
        const uint64_t lba = cur_offset / block_sz;
        Status st = read_blocks(lba, 1, bounce);
        if (st != Status::Ok) {
            return st;
        }
        const size_t avail = static_cast<size_t>(block_sz - lead_off);
        const size_t chunk = (remaining < avail) ? remaining : avail;
        copy_bytes(dst, bounce + lead_off, chunk);
        dst += chunk;
        cur_offset += chunk;
        remaining -= chunk;
    }

    // Full aligned blocks
    while (remaining >= block_sz) {
        const uint64_t lba = cur_offset / block_sz;
        const uint32_t full_blocks = static_cast<uint32_t>(remaining / block_sz);
        Status st = read_blocks(lba, full_blocks, dst);
        if (st != Status::Ok) {
            return st;
        }
        const size_t copied = static_cast<size_t>(full_blocks) * static_cast<size_t>(block_sz);
        dst += copied;
        cur_offset += copied;
        remaining -= copied;
    }

    // Trailing partial block
    if (remaining > 0) {
        const uint64_t lba = cur_offset / block_sz;
        Status st = read_blocks(lba, 1, bounce);
        if (st != Status::Ok) {
            return st;
        }
        copy_bytes(dst, bounce, remaining);
    }

    return Status::Ok;
}

Status BlockDevice::write_bytes(uint64_t offset, size_t size, const void* in_buf) {
    if (size == 0) {
        return Status::Ok;
    }
    if (in_buf == nullptr) {
        return Status::InvalidArg;
    }

    const DeviceInfo& dev_info = info();
    if (dev_info.is_read_only()) {
        return Status::ReadOnly;
    }

    const uint32_t block_sz = dev_info.block_size;
    if (block_sz == 0 || block_sz > kMaxBounceBlockSize) {
        return Status::Unsupported;
    }

    const uint64_t total = dev_info.total_bytes();
    if (offset >= total || static_cast<uint64_t>(size) > total - offset) {
        return Status::OutOfRange;
    }

    const auto* src = static_cast<const uint8_t*>(in_buf);
    size_t remaining = size;
    uint64_t cur_offset = offset;
    uint8_t bounce[kMaxBounceBlockSize];

    // Leading partial block (read-modify-write)
    const uint32_t lead_off = static_cast<uint32_t>(cur_offset % block_sz);
    if (lead_off != 0) {
        const uint64_t lba = cur_offset / block_sz;
        Status st = read_blocks(lba, 1, bounce);
        if (st != Status::Ok) {
            return st;
        }
        const size_t avail = static_cast<size_t>(block_sz - lead_off);
        const size_t chunk = (remaining < avail) ? remaining : avail;
        copy_bytes(bounce + lead_off, src, chunk);
        st = write_blocks(lba, 1, bounce);
        if (st != Status::Ok) {
            return st;
        }
        src += chunk;
        cur_offset += chunk;
        remaining
