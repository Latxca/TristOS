#include "fs.hpp"

namespace trist::fs {
namespace {

inline constexpr uint32_t kOnDiskMagic = 0x54465331u; // "TFS1"
inline constexpr uint32_t kOnDiskVersion = 1u;
inline constexpr uint64_t kSuperblockOffset = 0;

struct OnDiskSuperblock {
    uint32_t magic;
    uint32_t version;
    uint32_t next_inode;
    uint32_t record_count;
    uint64_t next_data_offset;
    uint64_t payload_region_offset;
    uint8_t volume_sig[kSignatureSize];
};

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

bool bytes_equal(const void* a, const void* b, size_t count) {
    const auto* pa = static_cast<const uint8_t*>(a);
    const auto* pb = static_cast<const uint8_t*>(b);
    for (size_t i = 0; i < count; ++i) {
        if (pa[i] != pb[i]) {
            return false;
        }
    }
    return true;
}

size_t cstr_len(const char* s) {
    if (s == nullptr) {
        return 0;
    }
    size_t len = 0;
    while (s[len] != '\0') {
        ++len;
    }
    return len;
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

bool ends_with_texe(const char* name) {
    const size_t len = cstr_len(name);
    constexpr size_t kExtLen = 5; // ".texe"
    if (len < kExtLen) {
        return false;
    }
    const char* tail = name + (len - kExtLen);
    return tail[0] == '.' && tail[1] == 't' && tail[2] == 'e' && tail[3] == 'x' && tail[4] == 'e';
}

uint64_t records_region_offset() {
    return sizeof(OnDiskSuperblock);
}

uint64_t default_payload_offset() {
    return sizeof(OnDiskSuperblock) + (sizeof(FileRecord) * kMaxFileRecords);
}

void mix_word(uint8_t sig[kSignatureSize], size_t idx, uint64_t val) {
    for (size_t b = 0; b < 8; ++b) {
        const size_t pos = (idx + b) % kSignatureSize;
        const uint8_t byte = static_cast<uint8_t>((val >> (b * 8)) & 0xFFu);
        sig[pos] = static_cast<uint8_t>(((sig[pos] << 3) | (sig[pos] >> 5)) ^ byte ^ static_cast<uint8_t>(pos * 29u + 7u));
    }
}

void compute_record_signature(const FileRecord& rec, uint8_t out_sig[kSignatureSize]) {
    zero_bytes(out_sig, kSignatureSize);
    for (size_t i = 0; i < kSignatureSize; ++i) {
        out_sig[i] = static_cast<uint8_t>(0xA5u ^ static_cast<uint8_t>(i * 17u + 3u));
    }

    mix_word(out_sig, 0, rec.inode);
    mix_word(out_sig, 4, rec.parent_inode);
    mix_word(out_sig, 8, static_cast<uint64_t>(rec.type));
    mix_word(out_sig, 12, rec.version);
    mix_word(out_sig, 16, rec.owner_uid);
    mix_word(out_sig, 20, rec.group_gid);
    mix_word(out_sig, 24, rec.size_bytes);
    mix_word(out_sig, 28, rec.data_offset);

    for (size_t i = 0; i < kMaxNameLen && rec.name[i] != '\0'; ++i) {
        const size_t pos = (i * 5) % kSignatureSize;
        const uint8_t ch = static_cast<uint8_t>(rec.name[i]);
        out_sig[pos] = static_cast<uint8_t>(((out_sig[pos] << 1) | (out_sig[pos] >> 7)) ^ ch ^ static_cast<uint8_t>(i + 11u));
    }

    const uint8_t flags_byte = static_cast<uint8_t>(
        (rec.world_readable ? 0x01u : 0x00u) |
        (rec.system_sealed ? 0x02u : 0x00u) |
        (rec.encrypted ? 0x04u : 0x00u));
    out_sig[kSignatureSize - 1] ^= flags_byte;
}

FileRecord* find_record_mut(Volume* vol, uint32_t inode) {
    if (vol == nullptr || inode == kInvalidInode) {
        return nullptr;
    }
    for (size_t i = 0; i < kMaxFileRecords; ++i) {
        if (vol->records[i].in_use && vol->records[i].inode == inode) {
            return &vol->records[i];
        }
    }
    return nullptr;
}

const FileRecord* find_record_const(const Volume* vol, uint32_t inode) {
    if (vol == nullptr || inode == kInvalidInode) {
        return nullptr;
    }
    for (size_t i = 0; i < kMaxFileRecords; ++i) {
        if (vol->records[i].in_use && vol->records[i].inode == inode) {
            return &vol->records[i];
        }
    }
    return nullptr;
}

Status allocate_record(Volume* vol,
                       uint32_t parent_inode,
                       const char* name,
                       EntryType type,
                       uint16_t version,
                       bool world_readable,
                       bool system_sealed,
                       bool encrypted,
                       uint32_t owner_uid,
                       uint32_t group_gid,
                       FileRecord** out_rec) {
    if (vol == nullptr || !vol->mounted || name == nullptr || name[0] == '\0') {
        return Status::InvalidArg;
    }
    if (cstr_len(name) >= kMaxNameLen) {
        return Status::InvalidArg;
    }

    if (find_child(vol, parent_inode, name) != nullptr) {
        return Status::AlreadyExists;
    }

    for (size_t i = 0; i < kMaxFileRecords; ++i) {
        if (!vol->records[i].in_use) {
            FileRecord& rec = vol->records[i];
            zero_bytes(&rec, sizeof(rec));
            rec.inode = vol->next_inode++;
            rec.parent_inode = parent_inode;
            rec.type = type;
            rec.version = version;
            rec.owner_uid = owner_uid;
            rec.group_gid = group_gid;
            rec.size_bytes = 0;
            rec.data_offset = vol->next_data_offset;
            rec.world_readable = world_readable;
            rec.system_sealed = system_sealed;
            rec.encrypted = encrypted;
            rec.in_use = true;
            copy_cstr(rec.name, sizeof(rec.name), name);
            compute_record_signature(rec, rec.integrity_sig);

            ++vol->record_count;
            vol->dirty = true;
            if (out_rec != nullptr) {
                *out_rec = &rec;
            }
            return Status::Ok;
        }
    }

    return Status::TableFull;
}

Status ensure_payload_capacity(Volume* vol, FileRecord* rec, uint64_t needed_end_offset) {
    if (vol == nullptr || rec == nullptr) {
        return Status::InvalidArg;
    }
    if (rec->data_offset == 0) {
        rec->data_offset = vol->next_data_offset;
    }

    if (vol->device != nullptr) {
        const uint64_t total_dev_bytes = vol->device->info().total_bytes();
        if (needed_end_offset > total_dev_bytes) {
            return Status::NoSpace;
        }
    }

    if (needed_end_offset > vol->next_data_offset) {
        vol->next_data_offset = needed_end_offset;
        vol->dirty = true;
    }
    return Status::Ok;
}

Status resolve_parent_and_leaf(const Volume* vol,
                               const char* path,
                               uint32_t* out_parent_inode,
                               char out_leaf[kMaxNameLen]) {
    if (vol == nullptr || !vol->mounted || path == nullptr || path[0] == '\0') {
        return Status::InvalidArg;
    }

    size_t len = cstr_len(path);
    if (len == 0 || len >= kMaxPathLen) {
        return Status::InvalidArg;
    }

    // Trim trailing slash unless path is "/"
    while (len > 1 && path[len - 1] == '/') {
        --len;
    }

    size_t last_slash = 0;
    bool has_slash = false;
    for (size_t i = 0; i < len; ++i) {
        if (path[i] == '/') {
            last_slash = i;
            has_slash = true;
        }
    }

    uint32_t cur_parent = kRootInode;
    size_t prefix_end = 0;
    if (has_slash) {
        prefix_end = (last_slash == 0) ? 1 : last_slash;
        if (prefix_end > 1) {
            // Walk intermediate directory segments
            size_t pos = (path[0] == '/') ? 1 : 0;
            while (pos < prefix_end) {
                char seg[kMaxNameLen];
                size_t seg_len = 0;
                while (pos < prefix_end && path[pos] != '/') {
                    if (seg_len + 1 >= kMaxNameLen) {
                        return Status::InvalidArg;
                    }
                    seg[seg_len++] = path[pos++];
                }
                seg[seg_len] = '\0';
                if (pos < prefix_end && path[pos] == '/') {
                    ++pos;
                }
                if (seg_len == 0 || cstr_eq(seg, ".")) {
                    continue;
                }
                const FileRecord* dir = find_child(vol, cur_parent, seg);
                if (dir == nullptr) {
                    return Status::NotFound;
                }
                if (dir->type != EntryType::Directory) {
                    return Status::NotADirectory;
                }
                cur_parent = dir->inode;
            }
        }
    }

    const size_t leaf_start = has_slash ? (last_slash + 1) : 0;
    if (leaf_start >= len) {
        return Status::InvalidArg;
    }
    const size_t leaf_len = len - leaf_start;
    if (leaf_len == 0 || leaf_len >= kMaxNameLen) {
        return Status::InvalidArg;
    }

    for (size_t i = 0; i < leaf_len; ++i) {
        out_leaf[i] = path[leaf_start + i];
    }
    out_leaf[leaf_len] = '\0';

    if (out_parent_inode != nullptr) {
        *out_parent_inode = cur_parent;
    }
    return Status::Ok;
}

} // namespace

Status bind_device(Volume* vol, tristos::storage::BlockDevice* device) {
    if (vol == nullptr) {
        return Status::InvalidArg;
    }
    vol->device = device;
    vol->dirty = true;
    return Status::Ok;
}

Status format(Volume* vol, tristos::storage::BlockDevice* device) {
    if (vol == nullptr) {
        return Status::InvalidArg;
    }

    tristos::storage::BlockDevice* target_dev = (device != nullptr) ? device : vol->device;
    zero_bytes(vol, sizeof(*vol));
    vol->magic = kVolumeMagic;
    vol->mounted = true;
    vol->dirty = true;
    vol->next_inode = kRootInode + 1;
    vol->next_data_offset = default_payload_offset();
    vol->record_count = 0;
    vol->device = target_dev;

    // Create root directory entry (inode 1)
    FileRecord& root = vol->records[0];
    root.inode = kRootInode;
    root.parent_inode = kRootInode;
    root.type = EntryType::Directory;
    root.version = 1;
    root.owner_uid = 0;
    root.group_gid = 0;
    root.size_bytes = 0;
    root.data_offset = 0;
    root.world_readable = true;
    root.system_sealed = true;
    root.encrypted = false;
    root.in_use = true;
    copy_cstr(root.name, sizeof(root.name), "/");
    compute_record_signature(root, root.integrity_sig);
    vol->record_count = 1;

    if (vol->device != nullptr) {
        return sync(vol);
    }
    return Status::Ok;
}

Status mount(Volume* vol, tristos::storage::BlockDevice* device) {
    if (vol == nullptr) {
        return Status::InvalidArg;
    }

    tristos::storage::BlockDevice* target_dev = (device != nullptr) ? device : vol->device;
    if (target_dev == nullptr) {
        if (vol->magic == kVolumeMagic) {
            vol->mounted = true;
            return Status::Ok;
        }
        return Status::InvalidArg;
    }

    OnDiskSuperblock sb{};
    tristos::storage::Status st = target_dev->read_bytes(kSuperblockOffset, sizeof(sb), &sb);
    if (st != tristos::storage::Status::Ok) {
        return Status::IoError;
    }

    if (sb.magic != kOnDiskMagic || sb.version != kOnDiskVersion) {
        return Status::IoError;
    }

    FileRecord staged_records[kMaxFileRecords];
    st = target_dev->read_bytes(records_region_offset(), sizeof(staged_records), staged_records);
    if (st != tristos::storage::Status::Ok) {
        return Status::IoError;
    }

    zero_bytes(vol, sizeof(*vol));
    vol->magic = kVolumeMagic;
    vol->mounted = true;
    vol->dirty = false;
    vol->next_inode = (sb.next_inode > kRootInode) ? sb.next_inode : (kRootInode + 1);
    vol->next_data_offset = (sb.next_data_offset >= default_payload_offset())
                                ? sb.next_data_offset
                                : default_payload_offset();
    vol->record_count = sb.record_count;
    vol->device = target_dev;
    copy_bytes(vol->records, staged_records, sizeof(staged_records));

    return Status::Ok;
}

Status sync(Volume* vol) {
    if (vol == nullptr || vol->magic != kVolumeMagic) {
        return Status::InvalidArg;
    }
    if (vol->device == nullptr) {
        vol->dirty = false;
        return Status::Ok;
    }

    if (vol->next_data_offset < default_payload_offset()) {
        vol->next_data_offset = default_payload_offset();
    }

    OnDiskSuperblock sb{};
    zero_bytes(&sb, sizeof(sb));
    sb.magic = kOnDiskMagic;
    sb.version = kOnDiskVersion;
    sb.next_inode = vol->next_inode;
    sb.record_count = vol->record_count;
    sb.next_data_offset = vol->next_data_offset;
    sb.payload_region_offset = default_payload_offset();

    for (size_t i = 0; i < kSignatureSize; ++i) {
        sb.volume_sig[i] = static_cast<uint8_t>(
            0x5Au ^ static_cast<uint8_t>(sb.record_count * 13u + i * 7u));
    }

    tristos::storage::Status st = vol->device->write_bytes(kSuperblockOffset, sizeof(sb), &sb);
    if (st != tristos::storage::Status::Ok) {
        return (st == tristos::storage::Status::ReadOnly) ? Status::AccessDenied : Status::IoError;
    }

    st = vol->device->write_bytes(records_region_offset(), sizeof(vol->records), vol->records);
    if (st != tristos::storage::Status::Ok) {
        return (st == tristos::storage::Status::ReadOnly) ? Status::AccessDenied : Status::IoError;
    }

    st = vol->device->flush();
    if (st != tristos::storage::Status::Ok) {
        return Status::IoError;
    }

    vol->dirty = false;
    return Status::Ok;
}

Status unmount(Volume* vol) {
    if (vol == nullptr) {
        return Status::InvalidArg;
    }
    if (!vol->mounted) {
        return Status::NotMounted;
    }

    if (vol->dirty && vol->device != nullptr) {
        Status sync_st = sync(vol);
        if (sync_st != Status::Ok) {
            return sync_st;
        }
    }

    for (size_t i = 0; i < kMaxOpenHandles; ++i) {
        vol->handles[i].in_use = false;
        vol->handles[i].handle_id = 0;
        vol->handles[i].inode = kInvalidInode;
        vol->handles[i].position = 0;
        vol->handles[i].flags = OpenFlags::None;
    }

    vol->mounted = false;
    return Status::Ok;
}

Status create_directory(Volume* vol,
                        uint32_t parent_inode,
                        const char* name,
                        uint32_t owner_uid,
                        uint32_t group_gid) {
    if (vol == nullptr || !vol->mounted) {
        return Status::NotMounted;
    }
    const FileRecord* parent = find_record_const(vol, parent_inode);
    if (parent == nullptr) {
        return Status::NotFound;
    }
    if (parent->type != EntryType::Directory) {
        return Status::NotADirectory;
    }

    FileRecord* created = nullptr;
    Status st = allocate_record(vol,
                                parent_inode,
                                name,
                                EntryType::Directory,
                                1,
                                true,
                                false,
                                false,
                                owner_uid,
                                group_gid,
                                &created);
    if (st != Status::Ok) {
        return st;
    }
    return sync(vol);
}

Status create(Volume* vol,
              const char* name,
              uint16_t version,
              bool world_readable) {
    return create_owned(vol,
                        kRootInode,
                        name,
                        version,
                        world_readable,
                        false,
                        0,
                        0);
}

Status create_owned(Volume* vol,
                    uint32_t parent_inode,
                    const char* name,
                    uint16_t version,
                    bool world_readable,
                    bool system_sealed,
                    uint32_t owner_uid,
                    uint32_t group_gid) {
    if (vol == nullptr || !vol->mounted) {
        return Status::NotMounted;
    }
    const FileRecord* parent = find_record_const(vol, parent_inode);
    if (parent == nullptr) {
        return Status::NotFound;
    }
    if (parent->type != EntryType::Directory) {
        return Status::NotADirectory;
    }

    const bool encrypt_file = system_sealed || ends_with_texe(name);
    FileRecord* created = nullptr;
    Status st = allocate_record(vol,
                                parent_inode,
                                name,
                                EntryType::File,
                                version,
                                world_readable,
                                system_sealed,
                                encrypt_file,
                                owner_uid,
                                group_gid,
                                &created);
    if (st != Status::Ok) {
        return st;
    }
    return sync(vol);
}

Status create_app_data(Volume* vol,
                       uint32_t parent_inode,
                       const char* app_name,
                       uint16_t version,
                       uint32_t group_gid) {
    if (vol == nullptr || !vol->mounted) {
        return Status::NotMounted;
    }
    const FileRecord* parent = find_record_const(vol, parent_inode);
    if (parent == nullptr) {
        return Status::NotFound;
    }
    if (parent->type != EntryType::Directory) {
        return Status::NotADirectory;
    }

    FileRecord* created = nullptr;
    Status st = allocate_record(vol,
                                parent_inode,
                                app_name,
                                EntryType::AppData,
                                version,
                                false,
                                false,
                                true,
                                parent->owner_uid,
                                group_gid,
                                &created);
    if (st != Status::Ok) {
        return st;
    }
    return sync(vol);
}

const FileRecord* find(const Volume* vol, const char* name) {
    if (vol == nullptr || name == nullptr || name[0] == '\0') {
        return nullptr;
    }
    if (cstr_eq(name, "/")) {
        return find_record_const(vol, kRootInode);
    }

    const FileRecord* in_root = find_child(vol, kRootInode, name);
    if (in_root != nullptr) {
        return in_root;
    }

    for (size_t i = 0; i < kMaxFileRecords; ++i) {
        if (vol->records[i].in_use && cstr_eq(vol->records[i].name, name)) {
            return &vol->records[i];
        }
    }
    return nullptr;
}

const FileRecord* find_child(const Volume* vol, uint32_t parent_inode, const char* name) {
    if (vol == nullptr || name == nullptr || name[0] == '\0') {
        return nullptr;
    }
    for (size_t i = 0; i < kMaxFileRecords; ++i) {
        const FileRecord& rec = vol->records[i];
        if (rec.in_use && rec.inode != kRootInode &&
            rec.parent_inode == parent_inode && cstr_eq(rec.name, name)) {
            return &rec;
        }
    }
    return nullptr;
}

const FileRecord* find_path(const Volume* vol, const char* path) {
    if (vol == nullptr || path == nullptr || path[0] == '\0') {
        return nullptr;
    }
    if (cstr_eq(path, "/")) {
        return find_record_const(vol, kRootInode);
    }

    uint32_t cur_inode = kRootInode;
    size_t i = (path[0] == '/') ? 1 : 0;
    const size_t len = cstr_len(path);

    while (i < len) {
        while (i < len && path[i] == '/') {
            ++i;
        }
        if (i >= len) {
            break;
        }

        char component[kMaxNameLen];
        size_t comp_len = 0;
        while (i < len && path[i] != '/') {
            if (comp_len + 1 >= kMaxNameLen) {
                return nullptr;
            }
            component[comp_len++] = path[i++];
        }
        component[comp_len] = '\0';

        if (cstr_eq(component, ".")) {
            continue;
        }

        const FileRecord* found = find_child(vol, cur_inode, component);
        if (found == nullptr) {
            return nullptr;
        }
        if (i >= len || (i + 1 == len && path[i] == '/')) {
            return found;
        }
        if (found->type != EntryType::Directory) {
            return nullptr;
        }
        cur_inode = found->inode;
    }

    return find_record_const(vol, cur_inode);
}

bool verify_signature(const Volume* vol, const FileRecord* record) {
    if (vol == nullptr || record == nullptr || !record->in_use) {
        return false;
    }
    uint8_t expected[kSignatureSize];
    compute_record_signature(*record, expected);
    return bytes_equal(record->integrity_sig, expected, kSignatureSize);
}

Status open(Volume* vol, const char* path, OpenFlags flags, FileHandle** out_handle) {
    if (vol == nullptr || out_handle == nullptr || path == nullptr || path[0] == '\0') {
        return Status::InvalidArg;
    }
    if (!vol->mounted) {
        return Status::NotMounted;
    }

    const bool want_read = has_flag(flags, OpenFlags::Read);
    const bool want_write = has_flag(flags, OpenFlags::Write) || has_flag(flags, OpenFlags::Append);
    const bool want_create = has_flag(flags, OpenFlags::Create);
    if (!want_read && !want_write && !want_create) {
        return Status::InvalidArg;
    }

    const FileRecord* existing = find_path(vol, path);
    FileRecord* target = nullptr;

    if (existing != nullptr) {
        if (existing->type == EntryType::Directory) {
            return Status::IsADirectory;
        }
        if (existing->system_sealed && want_write) {
            return Status::AccessDenied;
        }
        target = find_record_mut(vol, existing->inode);
    } else {
        if (!want_create) {
            return Status::NotFound;
        }
        uint32_t parent_inode = kRootInode;
        char leaf[kMaxNameLen];
        Status path_st = resolve_parent_and_leaf(vol, path, &parent_inode, leaf);
        if (path_st != Status::Ok) {
            return path_st;
        }
        Status create_st = create_owned(vol,
                                        parent_inode,
                                        leaf,
                                        1,
                                        true,
                                        false,
                                        0,
                                        0);
        if (create_st != Status::Ok) {
            return create_st;
        }
        target = find_record_mut(vol, find_child(vol, parent_inode, leaf)->inode);
    }

    if (target == nullptr) {
        return Status::NotFound;
    }

    if (has_flag(flags, OpenFlags::Truncate) && want_write) {
        target->size_bytes = 0;
        compute_record_signature(*target, target->integrity_sig);
        vol->dirty = true;
    }

    for (size_t i = 0; i < kMaxOpenHandles; ++i) {
        if (!vol->handles[i].in_use) {
            FileHandle& h = vol->handles[i];
            h.handle_id = static_cast<uint32_t>(i + 1u);
            h.inode = target->inode;
            h.position = has_flag(flags, OpenFlags::Append) ? target->size_bytes : 0;
            h.flags = flags;
            h.in_use = true;
            *out_handle = &h;
            return Status::Ok;
        }
    }

    return Status::TableFull;
}

Status read(Volume* vol, FileHandle* handle, size_t size, void* out_buf, size_t* out_read) {
    if (out_read != nullptr) {
        *out_read = 0;
    }
    if (vol == nullptr || handle == nullptr || !handle->in_use) {
        return Status::InvalidArg;
    }
    if (!vol->mounted) {
        return Status::NotMounted;
    }
    if (!has_flag(handle->flags, OpenFlags::Read)) {
        return Status::AccessDenied;
    }
    if (size > 0 && out_buf == nullptr) {
        return Status::InvalidArg;
    }
    if (vol->device == nullptr) {
        return Status::IoError;
    }

    const FileRecord* rec = find_record_const(vol, handle->inode);
    if (rec == nullptr || !rec->in_use) {
        return Status::NotFound;
    }
    if (rec->type == EntryType::Directory) {
        return Status::IsADirectory;
    }

    if (size == 0 || handle->position >= rec->size_bytes) {
        return Status::Ok;
    }

    const uint64_t available = rec->size_bytes - handle->position;
    const size_t to_read = (static_cast<uint64_t>(size) < available)
                               ? size
                               : static_cast<size_t>(available);

    const uint64_t dev_offset = rec->data_offset + handle->position;
    tristos::storage::Status st = vol->device->read_bytes(dev_offset, to_read, out_buf);
    if (st != tristos::storage::Status::Ok) {
        return Status::IoError;
    }

    handle->position += static_cast<uint64_t>(to_read);
    if (out_read != nullptr) {
        *out_read = to_read;
    }
    return Status::Ok;
}

Status write(Volume* vol, FileHandle* handle, size_t size, const void* in_buf, size_t* out_written) {
    if (out_written != nullptr) {
        *out_written = 0;
    }
    if (vol == nullptr || handle == nullptr || !handle->in_use) {
        return Status::InvalidArg;
    }
    if (!vol->mounted) {
        return Status::NotMounted;
    }
    if (!has_flag(handle->flags, OpenFlags::Write) && !has_flag(handle->flags, OpenFlags::Append)) {
        return Status::AccessDenied;
    }
    if (size > 0 && in_buf == nullptr) {
        return Status::InvalidArg;
    }
    if (vol->device == nullptr) {
        return Status::IoError;
    }

    FileRecord* rec = find_record_mut(vol, handle->inode);
    if (rec == nullptr || !rec->in_use) {
        return Status::NotFound;
    }
    if (rec->type == EntryType::Directory) {
        return Status::IsADirectory;
    }
    if (rec->system_sealed) {
        return Status::AccessDenied;
    }

    if (size == 0) {
        return Status::Ok;
    }

    if (has_flag(handle->flags, OpenFlags::Append)) {
        handle->position = rec->size_bytes;
    }

    const uint64_t end_rel = handle->position + static_cast<uint64_t>(size);
    const uint64_t end_abs = rec->data_offset + end_rel;
    Status cap_st = ensure_payload_capacity(vol, rec, end_abs);
    if (cap_st != Status::Ok) {
        return cap_st;
    }

    tristos::storage::Status st = vol->device->write_bytes(rec->data_offset + handle->position, size, in_buf);
    if (st != tristos::storage::Status::Ok) {
        return (st == tristos::storage::Status::ReadOnly) ? Status::AccessDenied : Status::IoError;
    }

    handle->position = end_rel;
    if (end_rel > rec->size_bytes) {
        rec->size_bytes = end_rel;
    }
    compute_record_signature(*rec, rec->integrity_sig);
    vol->dirty = true;

    if (out_written != nullptr) {
        *out_written = size;
    }
    return Status::Ok;
}

Status seek(Volume* vol, FileHandle* handle, uint64_t offset) {
    if (vol == nullptr || handle == nullptr || !handle->in_use) {
        return Status::InvalidArg;
    }
    if (!vol->mounted) {
        return Status::NotMounted;
    }
    const FileRecord* rec = find_record_const(vol, handle->inode);
    if (rec == nullptr) {
        return Status::NotFound;
    }
    if (offset > rec->size_bytes) {
        return Status::InvalidArg;
    }
    handle->position = offset;
    return Status::Ok;
}

Status close(Volume* vol, FileHandle* handle) {
    if (vol == nullptr || handle == nullptr) {
        return Status::InvalidArg;
    }
    for (size_t i = 0; i < kMaxOpenHandles; ++i) {
        if (&vol->handles[i] == handle) {
            if (!handle->in_use) {
                return Status::InvalidArg;
            }
            handle->in_use = false;
            handle->handle_id = 0;
            handle->inode = kInvalidInode;
            handle->position = 0;
            handle->flags = OpenFlags::None;
            if (vol->dirty && vol->device != nullptr) {
                return sync(vol);
            }
            return Status::Ok;
        }
    }
    return Status::InvalidArg;
}

Status read_file_bytes(Volume* vol,
                       const char* path,
                       uint64_t offset,
                       size_t size,
                       void* out_buf,
                       size_t* out_read) {
    if (out_read != nullptr) {
        *out_read = 0;
    }
    FileHandle* h = nullptr;
    Status st = open(vol, path, OpenFlags::Read, &h);
    if (st != Status::Ok) {
        return st;
    }
    st = seek(vol, h, offset);
    if (st == Status::Ok) {
        st = read(vol, h, size, out_buf, out_read);
    }
    Status close_st = close(vol, h);
    return (st == Status::Ok) ? close_st : st;
}

const char* status_to_string(Status status) {
    switch (status) {
        case Status::Ok:            return "Ok";
        case Status::InvalidArg:    return "InvalidArg";
        case Status::NotMounted:    return "NotMounted";
        case Status::AlreadyMounted:return "AlreadyMounted";
        case Status::NotFound:      return "NotFound";
        case Status::AlreadyExists: return "AlreadyExists";
        case Status::NotADirectory: return "NotADirectory";
        case Status::IsADirectory:  return "IsADirectory";
        case Status::NoSpace:       return "NoSpace";
        case Status::AccessDenied:  return "AccessDenied";
        case Status::IoError:       return "IoError";
        case Status::BadSignature:  return "BadSignature";
        case Status::TableFull:     return "TableFull";
        case Status::Unsupported:   return "Unsupported";
        default:                    return "UnknownStatus";
    }
}

} // namespace trist::fs
