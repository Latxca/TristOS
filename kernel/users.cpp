#include "users.hpp"
#include "kernel.hpp"
#include "clock.hpp"

namespace trist::users {

static void copy_text(char* destination, const char* source, uint32_t limit) {
    uint32_t i = 0;
    for (; i + 1 < limit && source[i] != 0; ++i) destination[i] = source[i];
    destination[i] = 0;
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

bool first_boot(const UserDatabase* database) {
    return database != nullptr && database->oobe_complete == 0;
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
    console_write_line("");
    console_write_line("TristOS first-boot experience");
    console_write_line("--------------------------------");
    console_write_line("Create your TristOS user account.");
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
    create(database, username, display_name, false);
    database->oobe_complete = 1;
    console_write_line("OOBE complete. Your standard user account is ready.");
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
