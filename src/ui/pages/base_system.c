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
    if (state->row == 3) {
        group = PKG_LOCAL_MIRROR_LIVE;
        page_show_packages(state, "Pacman packages - Local mirror bootstrap",
                           system->local_mirror ? &group : NULL,
                           system->local_mirror ? 1 : 0);
        return true;
    }
    if (state->row == 7) {
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
                       state->row == 3 || state->row == 7
        ? "Up/Down move   Enter packages   Space change   Esc back"
        : "Up/Down move   Enter/Space change   Esc back";

    draw_shell(state, "Base system", keys);
    draw_property_row(4, 0, state->row, "CPU platform", platform_name(system->platform));
    draw_property_row(6, 1, state->row, "Kernel", kernel_name(system->kernel));
    draw_property_row(8, 2, state->row, "Default locale", locale_name(system->locale));
    draw_property_row(10, 3, state->row, "Package source",
                      system->local_mirror ? "Local F2FS-DATA mirror" : "Network mirror");
    draw_property_row(12, 4, state->row, "Installed system mirrors",
                      system->china_mirrors ? "China mirror list" : "Keep current mirror list");
    draw_property_row(14, 5, state->row, "Bootloader", "systemd-boot");
    draw_property_row(16, 6, state->row, "Create EFI NVRAM entry",
                      system->create_efi_entry ? "Yes" : "No");
    draw_property_row(18, 7, state->row, "Shim/MOK (kernel only)",
                      system->secure_boot ? "Enabled" : "Disabled");
}

void handle_base_system(UiState *state, int key)
{
    SystemPlan *system = &state->plan->system;
    bool changed = false;

    if (key == 27) { state->screen = SCREEN_HOME; state->row = 1; return; }
    if (key == KEY_UP && state->row > 0) --state->row;
    else if (key == KEY_DOWN && state->row < 7) ++state->row;
    else if (page_enter_pressed(key) && inspect_packages(state)) return;
    else if (key == ' ' || key == KEY_LEFT || key == KEY_RIGHT || page_enter_pressed(key)) {
        switch (state->row) {
        case 0: system->platform = (Platform)(((int)system->platform + 1) % 3); changed = true; break;
        case 1: system->kernel = (Kernel)(((int)system->kernel + 1) % 4); changed = true; break;
        case 2: system->locale = system->locale == LOCALE_EN_US ? LOCALE_ZH_CN : LOCALE_EN_US; changed = true; break;
        case 3:
            if (system->local_mirror) {
                system->local_mirror = false;
                changed = true;
            } else if (confirm_dialog(
                           "Use local mirror",
                           "Use the local mirror only after independently verifying its "
                           "contents, completeness, compatibility, and usability. Detection "
                           "requires exactly one unused F2FS partition labelled F2FS-DATA, "
                           "containing repo/archlinux, outside every installation disk. The "
                           "Live environment temporarily disables package signature checking "
                           "only while bootstrapping the local HTTP server. Continue?")) {
                system->local_mirror = true;
                changed = true;
            } else {
                set_status(state, "Network mirror retained; local mirror was not accepted.");
            }
            break;
        case 4: system->china_mirrors = !system->china_mirrors; changed = true; break;
        case 5: break;
        case 6: system->create_efi_entry = !system->create_efi_entry; changed = true; break;
        case 7:
            if (system->secure_boot) {
                system->secure_boot = false;
                changed = true;
            } else if (confirm_dialog(
                           "Enable Secure Boot",
                           "Enable Secure Boot only after independently confirming that "
                           "shim-signed.pkg.tar.zst comes from a trusted source and is usable "
                           "on this system. The builder checks its package name and EFI "
                           "signature presence, but does not authenticate its source or inspect "
                           "its install scripts. Continue?")) {
                system->secure_boot = true;
                changed = true;
            } else {
                set_status(state, "Secure Boot remains disabled; shim-signed was not trusted.");
            }
            break;
        }
        if (changed) state->dirty = true;
    }
}
