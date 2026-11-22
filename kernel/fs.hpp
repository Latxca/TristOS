#pragma once

#include <stdint.h>

namespace trist::fs {

constexpr uint32_t kNtfsSignature = 0x5346544e;
constexpr uint32_t kMaxFiles = 64;
constexpr uint32_t kMaxAclEntries = 8;
constexpr uint32_t kRootInode = 1;
constexpr uint32_t kFlagDirectory = 1;
constexpr uint32_t kFlagEncrypted = 2;
constexpr uint32_t kFlagSystemProtected = 4;
constexpr uint32_t kPermissionRead = 1;
constexpr uint32_t kPermissionWrite = 2;
constexpr uint32_t kPermissionExecute = 4;
constexpr uint32_t kPermissionDelete = 8;
constexpr uint32_t kPermissionFullControl = 0x0f;

enum class PrincipalType : uint8_t { User, Group, Everyone };

struct AclEntry {
    PrincipalType principal_type;
    uint8_t allow;
    uint16_t reserved;
    uint32_t principal_id;
    uint32_t permissions;
};

struct SecurityDescriptor {
    uint32_t owner_user_id;
    uint32_t owner_group_id;
    uint32_t acl_count;
    AclEntry acl[kMaxAclEntries];
};

struct FileRecord {
    char name[32];
    uint32_t inode;
    uint32_t parent_inode;
    uint32_t size;
    uint32_t flags;
    uint8_t used;
    SecurityDescriptor security;
    uint64_t signature[4];
    };

struct Volume {
    uint32_t signature;
    uint32_t version;
    uint32_t next_inode;
    char drive_letter;
    FileRecord files[kMaxFiles];
};

void format(Volume* volume);
bool mount(Volume* volume);
bool create(Volume* volume, const char* name, uint32_t size, bool encrypted);
bool create_owned(Volume* volume, uint32_t parent_inode, const char* name, uint32_t size, bool directory, bool encrypted, uint32_t owner_user_id, uint32_t owner_group_id);
bool create_directory(Volume* volume, uint32_t parent_inode, const char* name, uint32_t owner_user_id, uint32_t owner_group_id);
bool create_app_data(Volume* volume, uint32_t user_directory_inode, const char* app_name, uint32_t owner_user_id, uint32_t owner_group_id);
const FileRecord* find(const Volume* volume, const char* name);
const FileRecord* find_child(const Volume* volume, uint32_t parent_inode, const char* name);
const FileRecord* find_path(const Volume* volume, const char* path);
bool set_owner(Volume* volume, uint32_t inode, uint32_t owner_user_id, uint32_t owner_group_id);
bool add_acl(Volume* volume, uint32_t inode, PrincipalType type, uint32_t principal_id, uint32_t permissions, bool allow);
bool can_access(const Volume* volume, const FileRecord* file, uint32_t user_id, uint32_t group_id, uint32_t requested_permissions);
bool sign_file(Volume* volume, uint32_t inode);
bool verify_signature(const Volume* volume, const FileRecord* file);
bool protect_system_file(Volume* volume, uint32_t inode);

}
