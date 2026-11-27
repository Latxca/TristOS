#pragma once

#include <stddef.h>
#include <stdint.h>

namespace tristos::storage {

inline constexpr size_t kMaxDeviceNameLen = 32;
inline constexpr size_t kMaxStorageDevices = 16;
inline constexpr uint32_t kDefaultSectorSize = 512;

enum class Status : uint8_t {
    Ok = 0,
    InvalidArg,
    OutOfRange,
    IoError,
    ReadOnly,
    NotFound,
    AlreadyExists,
    TableFull,
    Unsupported,
    NotReady
};

enum class DeviceKind : uint8_t {
    Unknown = 0,
    RamDisk,
    InitrdImage,
    AtaPio,
    AhciSata,
    Nvme,
    VirtioBlk,
    Loopback
};

enum class DeviceFlags : uint32_t {
    None = 0,
    ReadOnly = 1u << 0,
    Removable = 1u << 1,
    FlushRequired = 1u << 2,
    BootDevice = 1u << 3
};

constexpr DeviceFlags operator|(DeviceFlags a, DeviceFlags b) {
    return static_cast<DeviceFlags>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

constexpr DeviceFlags operator&(DeviceFlags a, DeviceFlags b) {
    return static_cast<DeviceFlags>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}

constexpr bool has_flag(DeviceFlags flags, DeviceFlags test) {
    return (static_cast<uint32_t>(flags) & static_cast<uint32_t>(test)) != 0;
}

struct DeviceInfo {
    char name[kMaxDeviceNameLen];
    uint32_t device_id;
    DeviceKind kind;
    DeviceFlags flags;
    uint32_t block_size;
    uint64_t block_count;

    uint64_t total_bytes() const {
        return static_cast<uint64_t>(block_size) * block_count;
    }

    bool is_read_only() const {
        return has_flag(flags, DeviceFlags::ReadOnly);
    }
};

class BlockDevice {
public:
    virtual ~BlockDevice() = default;

    virtual const DeviceInfo& info() const = 0;
    virtual Status read_blocks(uint64_t lba, uint32_t count, void* out_buf) = 0;
    virtual Status write_blocks(uint64_t lba, uint32_t count, const void* in_buf) = 0;
    virtual Status flush() { return Status::Ok; }

    // Byte-granularity helpers that handle partial blocks and bounce buffering
    Status read_bytes(uint64_t offset, size_t size, void* out_buf);
    Status write_bytes(uint64_t offset, size_t size, const void* in_buf);
};

struct RamDiskConfig {
    const char* name;
    uint8_t* buffer;
    size_t buffer_size;
    uint32_t block_size;
    DeviceFlags flags;
};

// Storage subsystem registry & management API
Status init();
Status register_device(BlockDevice* device, uint32_t* out_device_id = nullptr);
Status unregister_device(uint32_t device_id);

size_t device_count();
BlockDevice* get_device_by_index(size_t index);
BlockDevice* get_device_by_id(uint32_t device_id);
BlockDevice* get_device_by_name(const char* name);

// Convenience factory for an in-memory / boot-image block device
Status create_ram_disk(const RamDiskConfig& config, BlockDevice** out_device);
void destroy_ram_disk(BlockDevice* device);

// Human-readable status helper for kernel logging
const char* status_to_string(Status status);

} // namespace tristos::storage
