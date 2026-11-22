#pragma once

namespace trist::storage {

enum class InstallTarget { ExternalBootDevice, InternalDrive };

struct InstallPlan { InstallTarget target; char system_drive; char external_drive; bool valid; };

void choose_install_target(InstallPlan* plan);
void show_install_target(const InstallPlan* plan);

}
