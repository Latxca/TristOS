#include "fs.hpp"
#include <stddef.h>

namespace trist::fs {

static bool equal(const char* left, const char* right) {
    while (*left != 0 && *left == *right) { ++left; ++right; }
    return *left == *right;
}

static void copy_name(char* destination, const char* source) {
    uint32_t i = 0;
    for (; i < 31 && source[i] != 0; ++i) destination[i] = source[i];
    destination[i] = 0;
}

static void signature_for(const FileRecord* file, uint64_t output[4]) {
    output[0] = 0x243f6a8885a308d3ULL;
    output[1] = 0x13198a2e03707344ULL;
    output[2] = 0xa4093822299f31d0ULL;
    output[3] = 0x082efa98ec4e6c89ULL;
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(file);
    const uint32_t length = static_cast<uint32_t>(offsetof(FileRecord, signature));
    for (uint32_t i = 0; i < length; ++i) for (uint32_t lane = 0; lane < 4; ++lane) {
        output[lane] ^= bytes[i] + lane;
        output[lane] *= 0x100000001b3ULL;
        output[lane] ^= output[lane] >> 29;
    }
}

static bool signature_equal(const uint64_t left[4], const uint64_t right[4]) {
    return left[0] == right[0] && left[1] == right[1] && left[2] == right[2] && left[3] == right[3];
}

static FileRecord* mutable_find_inode(Volume* volume, uint32_t inode) {
    for (uint32_t i = 0; i < kMaxFiles; ++i)
        if (volume->files[i].used && volume->files[i].inode == inode) return &volume->files[i];
    return nullptr;
}

static void default_acl(SecurityDescriptor* security) {
    security->acl_count = 3;
    security->acl[0] = {PrincipalType::User, 1, 0, security->owner_user_id, kPermissionFullControl};
    security->acl[1] = {PrincipalType::Group, 1, 0, security->owner_group_id, kPermissionRead | kPermissionWrite | kPermissionExecute};
    security->acl[2] = {PrincipalType::Everyone, 1, 0, 0, kPermissionRead | kPermissionExecute};
}

void format(Volume* volume) {
    volume->signature = kNtfsSignature;
    volume->version = 2;
    volume->next_inode = kRootInode + 1;
    volume->drive_letter = 'T';
    for (uint32_t i = 0; i < kMaxFiles; ++i) volume->files[i].used = 0;
    FileRecord& root = volume->files[0];
    copy_name(root.name, "\\");
    root.inode = kRootInode;
    root.parent_inode = kRootInode;
    root.size = 0;
    root.flags = kFlagDirectory;
    root.used = 1;
    root.security.owner_user_id = 0;
    root.security.owner_group_id = 2;
    default_acl(&root.security);
    sign_file(volume, root.inode);
    }

bool mount(Volume* volume) {
    return volume != nullptr && volume->signature == kNtfsSignature && volume->version == 2;
}

const FileRecord* find_child(const Volume* volume, uint32_t parent_inode, const char* name) {
    if (!mount(const_cast<Volume*>(volume))) return nullptr;
    for (uint32_t i = 0; i < kMaxFiles; ++i)
        if (volume->files[i].used && volume->files[i].parent_inode == parent_inode && equal(volume->files[i].name, name)) return &volume->files[i];
    return nullptr;
}

const FileRecord* find(const Volume* volume, const char* name) {
    return find_child(volume, kRootInode, name);
}

const FileRecord* find_path(const Volume* volume, const char* path) {
    if (path == nullptr || path[0] != '/') return nullptr;
    uint32_t parent = kRootInode;
    uint32_t start = 1;
    while (path[start] != 0) {
        char component[32];
        uint32_t length = 0;
        while (path[start] != 0 && path[start] != '/' && length + 1 < sizeof(component)) component[length++] = path[start++];
        component[length] = 0;
        const FileRecord* found = find_child(volume, parent, component);
        if (found == nullptr) return nullptr;
        parent = found->inode;
        while (path[start] == '/') ++start;
        if (path[start] == 0) return found;
    }
    return find_child(volume, kRootInode, "\\");
}

bool create_owned(Volume* volume, uint32_t parent_inode, const char* name, uint32_t size, bool directory, bool encrypted, uint32_t owner_user_id, uint32_t owner_group_id) {
    if (!mount(volume) || name == nullptr || name[0] == 0 || mutable_find_inode(volume, parent_inode) == nullptr || find_child(volume, parent_inode, name) != nullptr) return false;
    for (uint32_t i = 0; i < kMaxFiles; ++i) {
        if (!volume->files[i].used) {
            FileRecord& file = volume->files[i];
            copy_name(file.name, name);
            file.inode = volume->next_inode++;
            file.parent_inode = parent_inode;
            file.size = size;
            file.flags = (directory ? kFlagDirectory : 0) | (encrypted ? kFlagEncrypted : 0);
            file.used = 1;
            file.security.owner_user_id = owner_user_id;
            file.security.owner_group_id = owner_group_id;
            default_acl(&file.security);
            if (owner_user_id == 0) file.flags |= kFlagSystemProtected;
            sign_file(volume, file.inode);
            return true;
        }
    }
    return false;
}

bool create(Volume* volume, const char* name, uint32_t size, bool encrypted) {
    return create_owned(volume, kRootInode, name, size, false, encrypted, 0, 2);
}

bool create_directory(Volume* volume, uint32_t parent_inode, const char* name, uint32_t owner_user_id, uint32_t owner_group_id) {
    return create_owned(volume, parent_inode, name, 0, true, false, owner_user_id, owner_group_id);
}

bool create_app_data(Volume* volume, uint32_t user_directory_inode, const char* app_name, uint32_t owner_user_id, uint32_t owner_group_id) {
    const FileRecord* app_data = find_child(volume, user_directory_inode, "AppData");
    if (app_data == nullptr) {
        if (!create_directory(volume, user_directory_inode, "AppData", owner_user_id, owner_group_id)) return false;
        app_data = find_child(volume, user_directory_inode, "AppData");
    }
    const FileRecord* local = find_child(volume, app_data->inode, "Local");
    if (local == nullptr) {
        if (!create_directory(volume, app_data->inode, "Local", owner_user_id, owner_group_id)) return false;
        local = find_child(volume, app_data->inode, "Local");
    }
    return local != nullptr && create_directory(volume, local->inode, app_name, owner_user_id, owner_group_id);
}

bool set_owner(Volume* volume, uint32_t inode, uint32_t owner_user_id, uint32_t owner_group_id) {
    FileRecord* file = mutable_find_inode(volume, inode);
    if (file == nullptr) return false;
    file->security.owner_user_id = owner_user_id;
    file->security.owner_group_id = owner_group_id;
    default_acl(&file->security);
    return true;
}

bool add_acl(Volume* volume, uint32_t inode, PrincipalType type, uint32_t principal_id, uint32_t permissions, bool allow) {
    FileRecord* file = mutable_find_inode(volume, inode);
    if (file == nullptr || file->security.acl_count >= kMaxAclEntries) return false;
    file->security.acl[file->security.acl_count++] = {type, static_cast<uint8_t>(allow ? 1 : 0), 0, principal_id, permissions};
    return true;
}

bool sign_file(Volume* volume, uint32_t inode) {
    FileRecord* file = mutable_find_inode(volume, inode);
    if (file == nullptr) return false;
    signature_for(file, file->signature);
    return true;
}

bool verify_signature(const Volume* volume, const FileRecord* file) {
    if (!mount(const_cast<Volume*>(volume)) || file == nullptr) return false;
    uint64_t expected[4];
    signature_for(file, expected);
    return signature_equal(expected, file->signature);
}

bool protect_system_file(Volume* volume, uint32_t inode) {
    FileRecord* file = mutable_find_inode(volume, inode);
    if (file == nullptr) return false;
    file->flags |= kFlagSystemProtected;
    return sign_file(volume, inode);
}

bool can_access(const Volume* volume, const FileRecord* file, uint32_t user_id, uint32_t group_id, uint32_t requested_permissions) {
    if (!mount(const_cast<Volume*>(volume)) || file == nullptr || !file->used) return false;
    if (file->security.owner_user_id == user_id) return true;
    uint32_t allowed = 0;
    for (uint32_t i = 0; i < file->security.acl_count; ++i) {
        const AclEntry& entry = file->security.acl[i];
        bool matches = entry.principal_type == PrincipalType::Everyone ||
                      (entry.principal_type == PrincipalType::User && entry.principal_id == user_id) ||
                      (entry.principal_type == PrincipalType::Group && entry.principal_id == group_id);
        if (matches) {
            if (!entry.allow && (entry.permissions & requested_permissions) != 0) return false;
            if (entry.allow) allowed |= entry.permissions;
        }
    }
    return (allowed & requested_permissions) == requested_permissions;
}

}
