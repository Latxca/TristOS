#pragma once

#include "storage.hpp"

#include <stddef.h>
#include <stdint.h>

namespace trist::fs {

inline constexpr uint32_t kRootInode = 1;
inline constexpr uint32_t kInvalidInode = 0;
inline constexpr size_t kMaxNameLen = 64;
inline constexpr size_t kMaxPathLen = 256;
inline constexpr size_t kMaxFileRecords = 256;
inline constexpr size_t kMaxOpenHandles = 64;
inline constexpr size_t kSignatureSize = 32;
inline constexpr uint32_t kVolumeMagic = 0x54525354u; // "TRST"

enum class Status : uint8_t {
    Ok = 0,
    InvalidArg,
    NotMounted,
    AlreadyMounted,
    NotFound,
    AlreadyExists,
    NotADirectory,
    IsADirectory,
    NoSpace,
    AccessDenied,
    IoError,
    BadSignature,
    TableFull,
    Unsupported
};

enum class EntryType : uint8_t {
    None = 0,
    File,
    Directory,
    AppData
};

enum class OpenFlags : uint32_t {
    None = 0,
    Read = 1u << 0,
    Write = 1u << 1,
    Create = 1u << 2,
    Truncate = 1u << 3,
    Append = 1u << 4
};

constexpr OpenFlags operator|(OpenFlags a, OpenFlags b) {
    return static_cast<OpenFlags>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

constexpr OpenFlags operator&(OpenFlags a, OpenFlags b) {
    return static_cast<OpenFlags>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}

constexpr bool has_flag(OpenFlags flags, OpenFlags test) {
    return (static_cast<uint32_t>(flags) & static_cast<uint32_t>(test)) != 0;
}

struct FileRecord {
    uint32_t inode;
    uint32_t parent_inode;
    EntryType type;
    uint16_t version;
    uint32_t owner_uid;
    uint32_t group_gid;
    uint64_t size_bytes;
    uint64_t data_offset;
    bool world_readable;
    bool system_sealed;
    bool encrypted;
    bool in_use;
    char name[kMaxNameLen];
    uint8_t integrity_sig[kSignatureSize];
};

struct FileHandle {
    uint32_t handle_id;
    uint32_t inode;
    uint64_t position;
    OpenFlags flags;
    bool in_use;
};

struct Volume {
    uint32_t magic;
    bool mounted;
    bool dirty;
    uint32_t next_inode;
    uint64_t next_data_offset;
    uint32_t record_count;
    tristos::storage::BlockDevice* device;
    FileRecord records[kMaxFileRecords];
    FileHandle handles[kMaxOpenHandles];
};

// Block device binding, formatting, mount & flush lifecycle
Status bind_device(Volume* vol, tristos::storage::BlockDevice* device);
Status format(Volume* vol, tristos::storage::BlockDevice* device = nullptr);
Status mount(Volume* vol, tristos::storage::BlockDevice* device = nullptr);
Status sync(Volume* vol);
Status unmount(Volume* vol);

// Directory & file creation API (matches main.cpp usage)
Status create_directory(Volume* vol,
                        uint32_t parent_inode,
                        const char* name,
                        uint32_t owner_uid,
                        uint32_t group_gid);

Status create(Volume* vol,
              const char* name,
              uint16_t version,
              bool world_readable);

Status create_owned(Volume* vol,
                    uint32_t parent_inode,
                    const char* name,
                    uint16_t version,
                    bool world_readable,
                    bool system_sealed,
                    uint32_t owner_uid,
                    uint32_t group_gid);

Status create_app_data(Volume* vol,
                       uint32_t parent_inode,
                       const char* app_name,
                       uint16_t version,
                       uint32_t group_gid);

// Lookup & integrity verification API
const FileRecord* find(const Volume* vol, const char* name);
const FileRecord* find_child(const Volume* vol, uint32_t parent_inode, const char* name);
const FileRecord* find_path(const Volume* vol, const char* path);
bool verify_signature(const Volume* vol, const FileRecord* record);

// Handle-based file read/write/seek/close for binaries (.texe) and data files
Status open(Volume* vol, const char* path, OpenFlags flags, FileHandle** out_handle);
Status read(Volume* vol, FileHandle* handle, size_t size, void* out_buf, size_t* out_read);
Status write(Volume* vol, FileHandle* handle, size_t size, const void* in_buf, size_t* out_written);
Status seek(Volume* vol, FileHandle* handle, uint64_t offset);
Status close(Volume* vol, FileHandle* handle);

// Read-all helper convenient for .texe loaders and config/lang files
Status read_file_bytes(Volume* vol,
                       const char* path,
                       uint64_t offset,
                       size_t size,
                       void* out_buf,
                       size_t* out_read);

const char* status_to_string(Status status);

} // namespace trist::fs
