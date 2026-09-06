#include "private.h"

/* 展示会影响基础安装包的行；Enter 只预览，Space 才修改选择。 */
static bool inspect_packages(UiState *state)
{
    SystemPlan *system = &state->plan->system;
    PackageGroup group;

    if (state->row == 0) {
        bool available = packages_platform_group(system->platform, &group);
        page_show_packages(state, "Pacman packages - CPU platform",
                           available ? &group : NULL, available ? 1 : 0);
        return true;
    }
    if (state->row == 1) {
        group = packages_kernel_group(system->kernel);
        page_show_packages(state, "Pacman packages - selected kernel", &group, 1);
        return true;
    }
    if (state->row == 4) {
        group = PKG_LOCAL_MIRROR_LIVE;
        page_show_packages(state, "Pacman packages - Local mirror bootstrap",
                           system->local_mirror ? &group : NULL,
                           system->local_mirror ? 1 : 0);
        return true;
    }
    if (state->row == 8) {
        group = PKG_SECURE_BOOT_LIVE;
        page_show_packages(state, "Pacman packages - Secure Boot", &group, 1);
        return true;
    }
    return false;
}

void draw_base_system(UiState *state)
{
    SystemPlan *system = &state->plan->system;
    const char *keys = state->row == 0 || state->row == 1 ||
                       state->row == 4 || state->row == 8
        ? "Up/Down move   Enter packages   Space change   Esc back"
        : "Up/Down move   Enter/Space change   Esc back";

    draw_shell(state, "Base system", keys);
    draw_property_row(4, 0, state->row, "CPU platform", platform_name(system->platform));
    draw_property_row(6, 1, state->row, "Kernel", kernel_name(system->kernel));
    draw_property_row(8, 2, state->row, "Default locale", locale_name(system->locale));
    draw_property_row(10, 3, state->row, "Timezone", system->timezone);
    draw_property_row(12, 4, state->row, "Package source",
                      system->local_mirror ? "Local F2FS-DATA mirror" : "Network mirror");
    draw_property_row(14, 5, state->row, "Installed system mirrors",
                      system->china_mirrors ? "China mirror list" : "Keep current mirror list");
    draw_property_row(16, 6, state->row, "Bootloader", "systemd-boot");
    draw_property_row(18, 7, state->row, "Create EFI NVRAM entry",
                      system->create_efi_entry ? "Yes" : "No");
    draw_property_row(20, 8, state->row, "Shim/MOK (kernel only)",
                      system->secure_boot ? "Enabled" : "Disabled");
}

void handle_base_system(UiState *state, int key)
{
    SystemPlan *system = &state->plan->system;
    bool changed = false;

    if (key == 27) { state->screen = SCREEN_HOME; state->row = 1; return; }
    if (key == KEY_UP && state->row > 0) --state->row;
    else if (key == KEY_DOWN && state->row < 8) ++state->row;
    else if (page_enter_pressed(key) && inspect_packages(state)) return;
    else if (key == ' ' || key == KEY_LEFT || key == KEY_RIGHT || page_enter_pressed(key)) {
        switch (state->row) {
        case 0: system->platform = (Platform)(((int)system->platform + 1) % 3); changed = true; break;
        case 1: system->kernel = (Kernel)(((int)system->kernel + 1) % 4); changed = true; break;
        case 2: system->locale = system->locale == LOCALE_EN_US ? LOCALE_ZH_CN : LOCALE_EN_US; changed = true; break;
        case 3: changed = text_dialog("Timezone (for example Asia/Shanghai)",
                                      system->timezone, sizeof(system->timezone)); break;
        case 4: system->local_mirror = !system->local_mirror; changed = true; break;
        case 5: system->china_mirrors = !system->china_mirrors; changed = true; break;
        case 6: break;
        case 7: system->create_efi_entry = !system->create_efi_entry; changed = true; break;
        case 8: system->secure_boot = !system->secure_boot; changed = true; break;
        }
        if (changed) state->dirty = true;
    }
}
