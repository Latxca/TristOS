#include "texe.hpp"

namespace tristos::texe {
namespace {

inline constexpr size_t kMaxStagedImages = 8;

struct StagingSlot {
    uint8_t buffer[kMaxTexeImageBytes];
    bool in_use;
};

StagingSlot g_staging_pool[kMaxStagedImages];

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

const char* basename_from_path(const char* path) {
    if (path == nullptr) {
        return "";
    }
    const char* last_sep = path;
    for (const char* p = path; *p != '\0'; ++p) {
        if (*p == '/' || *p == '\\') {
            last_sep = p + 1;
        }
    }
    return last_sep;
}

uint8_t* acquire_staging_buffer() {
    for (size_t i = 0; i < kMaxStagedImages; ++i) {
        if (!g_staging_pool[i].in_use) {
            g_staging_pool[i].in_use = true;
            zero_bytes(g_staging_pool[i].buffer, sizeof(g_staging_pool[i].buffer));
            return g_staging_pool[i].buffer;
        }
    }
    return nullptr;
}

bool release_staging_buffer(const uint8_t* ptr) {
    if (ptr == nullptr) {
        return false;
    }
    for (size_t i = 0; i < kMaxStagedImages; ++i) {
        if (g_staging_pool[i].in_use && g_staging_pool[i].buffer == ptr) {
            zero_bytes(g_staging_pool[i].buffer, sizeof(g_staging_pool[i].buffer));
            g_staging_pool[i].in_use = false;
            return true;
        }
    }
    return false;
}

Status read_entire_file(trist::fs::Volume* vol,
                        const char* path,
                        uint8_t* out_buf,
                        size_t capacity,
                        size_t* out_total_read) {
    if (out_total_read != nullptr) {
        *out_total_read = 0;
    }
    if (vol == nullptr || path == nullptr || path[0] == '\0' || out_buf == nullptr) {
        return Status::InvalidArg;
    }

    trist::fs::FileHandle* handle = nullptr;
    trist::fs::Status open_st = trist::fs::open(vol, path, trist::fs::OpenFlags::Read, &handle);
    if (open_st != trist::fs::Status::Ok) {
        if (open_st == trist::fs::Status::NotFound) {
            return Status::NotFound;
        }
        if (open_st == trist::fs::Status::InvalidArg || open_st == trist::fs::Status::NotMounted) {
            return Status::InvalidArg;
        }
        return Status::IoError;
    }

    size_t total = 0;
    Status final_st = Status::Ok;

    while (total < capacity) {
        size_t chunk_read = 0;
        trist::fs::Status rd_st = trist::fs::read(vol,
                                                 handle,
                                                 capacity - total,
                                                 out_buf + total,
                                                 &chunk_read);
        if (rd_st != trist::fs::Status::Ok) {
            final_st = Status::IoError;
            break;
        }
        total += chunk_read;
        if (chunk_read == 0) {
            break;
        }
    }

    // Check if extra data remains beyond capacity
    if (final_st == Status::Ok && total == capacity) {
        uint8_t probe = 0;
        size_t extra_read = 0;
        trist::fs::Status extra_st = trist::fs::read(vol, handle, 1, &probe, &extra_read);
        if (extra_st != trist::fs::Status::Ok) {
            final_st = Status::IoError;
        } else if (extra_read > 0) {
            final_st = Status::ImageTooLarge;
        }
    }

    trist::fs::Status close_st = trist::fs::close(vol, handle);
    if (final_st == Status::Ok && close_st != trist::fs::Status::Ok) {
        final_st = Status::IoError;
    }

    if (out_total_read != nullptr) {
        *out_total_read = total;
    }
    return final_st;
}

Status parse_header_and_metadata(trist::fs::Volume* vol,
                               const char* path,
                               const uint8_t* raw_bytes,
                               size_t raw_size,
                               bool require_signature,
                               bool allow_raw_payload,
                               ParsedImageInfo* out_info) {
    zero_bytes(out_info, sizeof(*out_info));
    out_info->file_size = raw_size;
    copy_cstr(out_info->name, sizeof(out_info->name), basename_from_path(path));

    const trist::fs::FileRecord* rec = trist::fs::find_path(vol, path);
    const bool sig_ok = (rec != nullptr) ? trist::fs::verify_signature(vol, rec) : false;
    out_info->signature_verified = sig_ok;

    if (require_signature && !sig_ok) {
        return Status::BadSignature;
    }

    if (raw_size >= sizeof(Header)) {
        Header hdr{};
        copy_bytes(&hdr, raw_bytes, sizeof(hdr));

        if (hdr.magic == kTexeMagic) {
            if (hdr.version != kTexeVersion1) {
                return Status::UnsupportedVersion;
            }
            if (hdr.header_size < sizeof(Header) || hdr.header_size > raw_size) {
                return Status::TruncatedImage;
            }
            if (hdr.code_offset > raw_size || hdr.code_size > raw_size - hdr.code_offset) {
                return Status::TruncatedImage;
            }

            out_info->version = hdr.version;
            out_info->subsystem = hdr.subsystem;
            out_info->flags = static_cast<ImageFlags>(hdr.flags);
            out_info->entry_offset = hdr.entry_offset;
            out_info->code_offset = hdr.code_offset;
            out_info->code_size = hdr.code_size;
            out_info->bss_size = hdr.bss_size;
            out_info->stack_reserve = hdr.stack_reserve;
            if (rec != nullptr && rec->system_sealed) {
                out_info->flags = out_info->flags | ImageFlags::SystemSealed;
            }
            return Status::Ok;
        }
    }

    if (!allow_raw_payload) {
        return Status::BadMagic;
    }

    // Fallback metadata for small stub/raw .texe entries
    out_info->version = kTexeVersion1;
    out_info->subsystem = Subsystem::UserApp;
    out_info->flags = ImageFlags::UserMode;
    if (rec != nullptr && rec->system_sealed) {
        out_info->flags = out_info->flags | ImageFlags::SystemSealed;
    }
    out_info->entry_offset = 0;
    out_info->code_offset = 0;
    out_info->code_size = raw_size;
    out_info->bss_size = 0;
    out_info->stack_reserve = 0;
    return Status::Ok;
}

} // namespace

Status inspect_file(trist::fs::Volume* vol,
                    const char* path,
                    bool require_signature,
                    ParsedImageInfo* out_info) {
    if (vol == nullptr || path == nullptr || path[0] == '\0' || out_info == nullptr) {
        return Status::InvalidArg;
    }

    uint8_t* temp = acquire_staging_buffer();
    if (temp == nullptr) {
        return Status::IoError;
    }

    size_t total_read = 0;
    Status rd_st = read_entire_file(vol, path, temp, sizeof(g_staging_pool[0].buffer), &total_read);
    if (rd_st != Status::Ok) {
        release_staging_buffer(temp);
        return rd_st;
    }

    Status st = parse_header_and_metadata(vol,
                                          path,
                                          temp,
                                          total_read,
                                          require_signature,
                                          true,
                                          out_info);
    release_staging_buffer(temp);
    return st;
}

Status load_from_volume(trist::fs::Volume* vol,
                        const char* path,
                        const LoadOptions& options,
                        LoadedImage* out_image) {
    if (vol == nullptr || path == nullptr || path[0] == '\0' || out_image == nullptr) {
        return Status::InvalidArg;
    }

    zero_bytes(out_image, sizeof(*out_image));

    uint8_t* raw_temp = acquire_staging_buffer();
    if (raw_temp == nullptr) {
        return Status::IoError;
    }

    size_t total_read = 0;
    Status rd_st = read_entire_file(vol, path, raw_temp, sizeof(g_staging_pool[0].buffer), &total_read);
    if (rd_st != Status::Ok) {
        release_staging_buffer(raw_temp);
        return rd_st;
    }

    ParsedImageInfo parsed{};
    Status parse_st = parse_header_and_metadata(vol,
                                                path,
                                                raw_temp,
                                                total_read,
                                                options.require_signature,
                                                options.allow_raw_payload,
                                                &parsed);
    if (parse_st != Status::Ok) {
        release_staging_buffer(raw_temp);
        return parse_st;
    }

    const size_t slice_size = static_cast<size_t>(parsed.code_size);
    const size_t slice_off = static_cast<size_t>(parsed.code_offset);

    if (options.destination_buf != nullptr) {
        if (options.destination_capacity < slice_size) {
            release_staging_buffer(raw_temp);
            return Status::BufferTooSmall;
        }
        zero_bytes(options.destination_buf, options.destination_capacity);
        if (slice_size > 0) {
            copy_bytes(options.destination_buf, raw_temp + slice_off, slice_size);
        }
        release_staging_buffer(raw_temp);

        out_image->info = parsed;
        out_image->payload = options.destination_buf;
        out_image->payload_size = slice_size;
        out_image->owned_by_loader = false;
        return Status::Ok;
    }

    // Reuse raw_temp directly as the loader-owned staging buffer when no destination buffer is given
    if (slice_off > 0 && slice_size > 0) {
        uint8_t shifted[kMaxTexeImageBytes];
        zero_bytes(shifted, sizeof(shifted));
        copy_bytes(shifted, raw_temp + slice_off, slice_size);
        zero_bytes(raw_temp, sizeof(g_staging_pool[0].buffer));
        copy_bytes(raw_temp, shifted, slice_size);
    }

    out_image->info = parsed;
    out_image->payload = raw_temp;
    out_image->payload_size = slice_size;
    out_image->owned_by_loader = true;
    return Status::Ok;
}

void release_image(LoadedImage* image) {
    if (image == nullptr) {
        return;
    }
    if (image->owned_by_loader && image->payload != nullptr) {
        release_staging_buffer(image->payload);
    }
    zero_bytes(image, sizeof(*image));
}

const char* status_to_string(Status status) {
    switch (status) {
        case Status::Ok:                 return "Ok";
        case Status::InvalidArg:         return "InvalidArg";
        case Status::NotFound:           return "NotFound";
        case Status::BadMagic:           return "BadMagic";
        case Status::UnsupportedVersion: return "UnsupportedVersion";
        case Status::TruncatedImage:     return "TruncatedImage";
        case Status::ImageTooLarge:      return "ImageTooLarge";
        case Status::BadSignature:       return "BadSignature";
        case Status::IoError:            return "IoError";
        case Status::BufferTooSmall:     return "BufferTooSmall";
        default:                         return "UnknownStatus";
    }
}

} // namespace tristos::texe
