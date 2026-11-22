#include "users.hpp"
#include "kernel.hpp"
#include "clock.hpp"
#include "storage.hpp"

namespace trist::users {

static void copy_text(char* destination, const char* source, uint32_t limit) {
    uint32_t i = 0;
    for (; i + 1 < limit && source[i] != 0; ++i) destination[i] = source[i];
    destination[i] = 0;
}

static bool same_text(const char* left, const char* right) {
    uint32_t i = 0;
    while (left[i] != 0 && left[i] == right[i]) ++i;
    return left[i] == 0 && right[i] == 0;
}

void initialize(UserDatabase* database) {
    database->version = 1;
    database->oobe_complete = 0;
    for (uint32_t i = 0; i < kMaxUsers; ++i) database->accounts[i].enabled = 0;
    copy_text(database->accounts[0].name, "administrator", sizeof(database->accounts[0].name));
    copy_text(database->accounts[0].display_name, "Administrator", sizeof(database->accounts[0].display_name));
    database->accounts[0].groups = kGroupAdmins;
    database->accounts[0].permissions = kPermissionRead | kPermissionWrite | kPermissionExecute | kPermissionManageUsers;
    database->accounts[0].hidden = 1;
}

static void hash_password(const char* password, uint64_t output[4]) {
    output[0] = 0x243f6a8885a308d3ULL;
    output[1] = 0x13198a2e03707344ULL;
    output[2] = 0xa4093822299f31d0ULL;
    output[3] = 0x082efa98ec4e6c89ULL;
    for (uint32_t i = 0; password[i] != 0; ++i) {
        for (uint32_t lane = 0; lane < 4; ++lane) {
            output[lane] ^= static_cast<uint8_t>(password[i]) + lane;
            output[lane] *= 0x100000001b3ULL;
            output[lane] ^= output[lane] >> 29;
        }
    }
}

bool first_boot(const UserDatabase* database) {
    return database != nullptr && database->oobe_complete == 0;
}

bool set_password(User* user, const char* password) {
    if (user == nullptr || password == nullptr || password[0] == 0) return false;
    hash_password(password, user->password_hash);
    return true;
}

User* find(UserDatabase* database, const char* name) {
    for (uint32_t i = 0; i < kMaxUsers; ++i)
        if (database->accounts[i].enabled && !database->accounts[i].hidden) {
            uint32_t j = 0;
            while (database->accounts[i].name[j] != 0 && database->accounts[i].name[j] == name[j]) ++j;
            if (database->accounts[i].name[j] == 0 && name[j] == 0) return &database->accounts[i];
        }
    return nullptr;
}

bool create(UserDatabase* database, const char* name, const char* display_name, bool administrator) {
    for (uint32_t i = 1; i < kMaxUsers; ++i) {
        if (!database->accounts[i].enabled) {
            copy_text(database->accounts[i].name, name, sizeof(database->accounts[i].name));
            copy_text(database->accounts[i].display_name, display_name, sizeof(database->accounts[i].display_name));
            database->accounts[i].groups = administrator ? kGroupAdmins : kGroupUsers;
            database->accounts[i].permissions = administrator ? (kPermissionRead | kPermissionWrite | kPermissionExecute | kPermissionManageUsers) : (kPermissionRead | kPermissionExecute);
            database->accounts[i].hidden = 0;
            database->accounts[i].enabled = 1;
            return true;
        }
    }
    return false;
}

void run_oobe(UserDatabase* database) {
    char username[24];
    char display_name[32];
    char date_time[32];
    char password[64];
    char confirmation[64];
    console_write_line("");
    console_write_line("TristOS first-boot experience");
    console_write_line("--------------------------------");
    console_write_line("Create your TristOS user account.");
    storage::InstallPlan plan;
    storage::choose_install_target(&plan);
    storage::show_install_target(&plan);
    do {
        console_write("Username: ");
        console_read_line(username, sizeof(username));
    } while (username[0] == 0);
    do {
        console_write("Display name: ");
        console_read_line(display_name, sizeof(display_name));
    } while (display_name[0] == 0);
    console_write_line("Set date and time as HH:MM:SS DD/MM/YY.");
    bool valid_time = false;
    do {
        console_write("Date/time: ");
        console_read_line(date_time, sizeof(date_time));
        valid_time = clock::set_from_text(date_time);
        if (!valid_time) console_write_line("Invalid date/time. Try again.");
    } while (!valid_time);
    do {
        console_write("Password: ");
        console_read_line(password, sizeof(password), false);
        console_write("Confirm password: ");
        console_read_line(confirmation, sizeof(confirmation), false);
        if (password[0] == 0 || !same_text(password, confirmation)) console_write_line("Passwords must match and cannot be empty.");
        } while (password[0] == 0 || !same_text(password, confirmation));
    create(database, username, display_name, true);
    User* created = find(database, username);
    set_password(created, password);
    database->oobe_complete = 1;
    console_write_line("OOBE complete. Your administrator account is ready.");
}

void show_accounts(const UserDatabase* database) {
    console_write_line("Local users:");
    for (uint32_t i = 0; i < kMaxUsers; ++i) {
        if (database->accounts[i].enabled && !database->accounts[i].hidden) {
            console_write("  ");
            console_write(database->accounts[i].display_name);
            console_write_line((database->accounts[i].groups & kGroupAdmins) != 0 ? " [admins]" : " [users]");
        }
    }
}

void show_groups() {
    console_write_line("User groups and permissions:");
    console_write_line("  users  : read, execute");
    console_write_line("  admins : read, write, execute, manage-users");
}

}
