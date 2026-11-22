#include "kernel.hpp"
#include "settings.hpp"

namespace trist {

static users::User* active_user;

void set_active_user(users::User* user) { active_user = user; }

void change_password() {
    if (active_user == nullptr) {
        console_write_line("Password: no active user");
        return;
    }
    char password[64];
    char confirmation[64];
    console_write("New password: ");
    console_read_line(password, sizeof(password), false);
    console_write("Confirm password: ");
    console_read_line(confirmation, sizeof(confirmation), false);
    uint32_t i = 0;
    while (password[i] != 0 && password[i] == confirmation[i]) ++i;
    if (password[i] == 0 && confirmation[i] == 0 && password[0] != 0 && users::set_password(active_user, password))
        console_write_line("Password changed.");
    else
        console_write_line("Passwords did not match or were empty.");
}

void settings_app() {
    console_write_line("Settings");
    console_write_line("  password - change your account password");
    console_write_line("  account  - show the current account");
    console_write("settings> ");
    char command[32];
    console_read_line(command, sizeof(command));
    if (command[0] == 'p') change_password();
    else if (command[0] == 'a' && active_user != nullptr) {
        console_write("Signed in as: ");
        console_write_line(active_user->display_name);
    }
}

}
