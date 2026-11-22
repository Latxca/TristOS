#pragma once

#include <stdint.h>

namespace trist::users {

constexpr uint32_t kMaxUsers = 8;
constexpr uint32_t kGroupUsers = 1;
constexpr uint32_t kGroupAdmins = 2;
constexpr uint32_t kPermissionRead = 1;
constexpr uint32_t kPermissionWrite = 2;
constexpr uint32_t kPermissionExecute = 4;
constexpr uint32_t kPermissionManageUsers = 8;

struct User {
    char name[24];
    char display_name[32];
    uint32_t groups;
    uint32_t permissions;
    uint64_t password_hash[4];
    uint8_t hidden;
    uint8_t enabled;
};

struct UserDatabase {
    uint32_t version;
    uint8_t oobe_complete;
    User accounts[kMaxUsers];
};

void initialize(UserDatabase* database);
bool first_boot(const UserDatabase* database);
bool create(UserDatabase* database, const char* name, const char* display_name, bool administrator);
bool set_password(User* user, const char* password);
User* find(UserDatabase* database, const char* name);
void run_oobe(UserDatabase* database);
void show_accounts(const UserDatabase* database);
void show_groups();

}
