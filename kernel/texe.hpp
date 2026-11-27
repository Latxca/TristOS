#pragma once

#include "fs.hpp"

#include <stddef.h>
#include <stdint.h>

namespace tristos::texe {

inline constexpr uint32_t kTexeMagic = 0x45584554u; // "TEXE" little-endian
inline constexpr uint16_t kTexeVersion1 = 1u;
inline constexpr size_t kMaxTexeImageBytes = 256 * 1024;
inline constexpr size_t kMaxTexeNameLen = 64;

enum class Status : uint8_t {
    Ok = 0,
    InvalidArg,
    NotFound,
    BadMagic,
    UnsupportedVersion,
    TruncatedImage,
    ImageTooLarge,
    BadSignature,
    IoError,
    BufferTooSmall
};

enum class Subsystem : uint8_t {
    Unknown = 0,
    KernelService,
    SystemDaemon,
    ShellOrUi,
    UserApp
};

enum class ImageFlags : uint32_t {
    None = 0,
    SystemSealed = 1u << 0,
    BackgroundDaemon = 1u << 1,
    UserMode = 1u << 2,
    RequireSignature = 1u << 3
};

constexpr ImageFlags operator|(ImageFlags a, ImageFlags b) {
    return static_cast<ImageFlags>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

constexpr ImageFlags operator&(ImageFlags a, ImageFlags b) {
    return static_cast<ImageFlags>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}

constexpr bool has_flag(ImageFlags flags, ImageFlags test) {
    return (static_cast<uint32_t>(flags) & static_cast<uint32_t>(test)) != 0;
}

// On-disk header at offset 0 of a .texe file (version 1)
struct Header {
    uint32_t magic;          // kTexeMagic ("TEXE")
    uint16_t version;        // kTexeVersion1 (1)
    uint16_t header_size;    // sizeof(Header)
    Subsystem subsystem;     // Target role/subsystem
    uint8_t reserved0[3];
    uint32_t flags;          // ImageFlags bitmask
    uint64_t entry_offset;   // Byte offset from image start (or virtual entry VA)
    uint64_t code_offset;    // Offset to code/text payload within file
    uint64_t code_size;      // Size of initialized code/data payload in bytes
    uint64_t bss_size;       // Zero-initialized BSS bytes needed after payload
    uint64_t stack_reserve;  // Requested stack size in bytes (0 = default)
};

static_assert(sizeof(Header) == 56, "Unexpected .texe Header size");

struct ParsedImageInfo {
    uint16_t version;
    Subsystem subsystem;
    ImageFlags flags;
    uint64_t file_size;
    uint64_t entry_offset;
    uint64_t code_offset;
    uint64_t code_size;
    uint64_t bss_size;
    uint64_t stack_reserve;
    bool signature_verified;
    char name[kMaxTexeNameLen];
};

struct LoadOptions {
    bool require_signature;      // Enforce fs::verify_signature before loading
    bool allow_raw_payload;      // Fallback if file is a stub/raw blob without TEXE header
    uint8_t* destination_buf;    // Optional caller-provided load buffer
    size_t destination_capacity; // Capacity of destination_buf in bytes
};

struct LoadedImage {
    ParsedImageInfo info;
    const uint8_t* payload;      // Points into destination_buf or internal/static buffer
    size_t payload_size;         // Bytes loaded into payload
    bool owned_by_loader;        // True if loader placed bytes in internal staging buffer
};

// Inspect metadata & validate header/signature without loading full payload
Status inspect_file(trist::fs::Volume* vol,
                    const char* path,
                    bool require_signature,
                    ParsedImageInfo* out_info);

// Load a .texe file from trist::fs::Volume using fs::open, fs::read, and fs::close
Status load_from_volume(trist::fs::Volume* vol,
                        const char* path,
                        const LoadOptions& options,
                        LoadedImage* out_image);

// Release internal staging slot if owned_by_loader is true
void release_image(LoadedImage* image);

const char* status_to_string(Status status);

} // namespace tristos::texe
