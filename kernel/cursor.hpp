#include "cursor.hpp"
#include "fs.hpp"

namespace tristos::cursor {
namespace {

inline constexpr size_t kMaxCursorFileBytes = 1024 * 1024; // 1 MiB safety cap for freestanding buffers
inline constexpr size_t kMaxLoadedCursors = 8;

struct CursorSlot {
    uint8_t buffer[kMaxCursorFileBytes];
    bool in_use;
};

CursorSlot g_cursor_pool[kMaxLoadedCursors];

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

uint8_t* acquire_cursor_slot() {
    for (size_t i = 0; i < kMaxLoadedCursors; ++i) {
        if (!g_cursor_pool[i].in_use) {
            g_cursor_pool[i].in_use = true;
            zero_bytes(g_cursor_pool[i].buffer, sizeof(g_cursor_pool[i].buffer));
            return g_cursor_pool[i].buffer;
        }
    }
    return nullptr;
}

bool release_cursor_slot(const uint8_t* ptr) {
    if (ptr == nullptr) {
        return false;
    }
    for (size_t i = 0; i < kMaxLoadedCursors; ++i) {
        if (g_cursor_pool[i].in_use && g_cursor_pool[i].buffer == ptr) {
            zero_bytes(g_cursor_pool[i].buffer, sizeof(g_cursor_pool[i].buffer));
            g_cursor_pool[i].in_use = false;
            return true;
        }
    }
    return false;
}

} // namespace

Status parse_buffer(const uint8_t* data, size_t size, ParsedCursor* out_cursor) {
    if (data == nullptr || out_cursor == nullptr) {
        return Status::InvalidArg;
    }
    if (size < sizeof(Header)) {
        return Status::TruncatedData;
    }

    Header hdr{};
    copy_bytes(&hdr, data, sizeof(hdr));

    if (hdr.magic != kTcurMagic) {
        return Status::BadMagic;
    }
    if (hdr.version != kTcurVersion1) {
        return Status::UnsupportedVersion;
    }
    if (hdr.header_size < sizeof(Header) || hdr.header_size > size) {
        return Status::TruncatedData;
    }
    if (hdr.width == 0 || hdr.height == 0 || hdr.frame_count == 0) {
        return Status::InvalidArg;
    }

    const uint64_t frame_pixels = static_cast<uint64_t>(hdr.width) * static_cast<uint64_t>(hdr.height);
    if (frame_pixels == 0 || frame_pixels > (static_cast<uint64_t>(SIZE_MAX) / sizeof(uint32_t))) {
        return Status::BufferTooSmall;
    }
    if (static_cast<uint64_t>(hdr.frame_count) >
        (static_cast<uint64_t>(SIZE_MAX) / sizeof(uint32_t)) / frame_pixels) {
        return Status::BufferTooSmall;
    }

    const size_t total_pixels = static_cast<size_t>(frame_pixels * static_cast<uint64_t>(hdr.frame_count));
    const size_t pixel_bytes = total_pixels * sizeof(uint32_t);
    const size_t payload_offset = static_cast<size_t>(hdr.header_size);

    if (pixel_bytes > size - payload_offset) {
        return Status::TruncatedData;
    }

    zero_bytes(out_cursor, sizeof(*out_cursor));
    out_cursor->width = hdr.width;
    out_cursor->height = hdr.height;
    out_cursor->hotspot_x = hdr.hotspot_x;
    out_cursor->hotspot_y = hdr.hotspot_y;
    out_cursor->frame_count = hdr.frame_count;
    out_cursor->frame_delay_ms = hdr.frame_delay_ms;
    out_cursor->pixels = reinterpret_cast<const uint32_t*>(data + payload_offset);
    out_cursor->pixel_count = total_pixels;
    return Status::Ok;
}

Status load_from_volume(trist::fs::Volume* vol, const char* path, ParsedCursor* out_cursor) {
    if (vol == nullptr || path == nullptr || path[0] == '\0' || out_cursor == nullptr) {
        return Status::InvalidArg;
    }

    uint8_t* buf = acquire_cursor_slot();
    if (buf == nullptr) {
        return Status::BufferTooSmall;
    }

    trist::fs::FileHandle* handle = nullptr;
    trist::fs::Status open_st = trist::fs::open(vol, path, trist::fs::OpenFlags::Read, &handle);
    if (open_st != trist::fs::Status::Ok) {
        release_cursor_slot(buf);
        return Status::TruncatedData;
    }

    size_t total_read = 0;
    bool read_ok = true;
    while (total_read < sizeof(g_cursor_pool[0].buffer)) {
        size_t chunk = 0;
        trist::fs::Status rd_st = trist::fs::read(vol,
                                                 handle,
                                                 sizeof(g_cursor_pool[0].buffer) - total_read,
                                                 buf + total_read,
                                                 &chunk);
        if (rd_st != trist::fs::Status::Ok) {
            read_ok = false;
            break;
        }
        total_read += chunk;
        if (chunk == 0) {
            break;
        }
    }

    trist::fs::close(vol, handle);

    if (!read_ok) {
        release_cursor_slot(buf);
        return Status::TruncatedData;
    }

    Status parse_st = parse_buffer(buf, total_read, out_cursor);
    if (parse_st != Status::Ok) {
        release_cursor_slot(buf);
        return parse_st;
    }

    return Status::Ok;
}

void release_cursor(ParsedCursor* cursor) {
    if (cursor == nullptr) {
        return;
    }
    const auto* raw_ptr = reinterpret_cast<const uint8_t*>(cursor->pixels);
    if (raw_ptr != nullptr) {
        for (size_t i = 0; i < kMaxLoadedCursors; ++i) {
            if (g_cursor_pool[i].in_use &&
                raw_ptr >= g_cursor_pool[i].buffer &&
                raw_ptr < g_cursor_pool[i].buffer + sizeof(g_cursor_pool[i].buffer)) {
                release_cursor_slot(g_cursor_pool[i].buffer);
                break;
            }
        }
    }
    zero_bytes(cursor, sizeof(*cursor));
}

const char* status_to_string(Status status) {
    switch (status) {
        case Status::Ok:                 return "Ok";
        case Status::InvalidArg:         return "InvalidArg";
        case Status::BadMagic:           return "BadMagic";
        case Status::UnsupportedVersion: return "UnsupportedVersion";
        case Status::TruncatedData:     return "TruncatedData";
        case Status::BufferTooSmall:     return "BufferTooSmall";
        default:                         return "UnknownStatus";
    }
}

} // namespace tristos::cursor

#include "fs.hpp"
...
Status load_from_volume(trist::fs::Volume* vol, const char* path, ParsedCursor* out_cursor);
void release_cursor(ParsedCursor* cursor);
