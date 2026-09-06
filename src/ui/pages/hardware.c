#include "private.h"

void draw_hardware(UiState *state)
{
    SystemPlan *system = &state->plan->system;

    draw_shell(state, "Hardware support",
               "Up/Down move   Enter packages   Space toggle   Esc back");
    draw_property_row(5, 0, state->row, "Device type",
                      system->laptop ? "Laptop (TLP enabled)" : "Desktop");
    draw_property_row(7, 1, state->row, "Intel integrated graphics",
                      system->intel_graphics ? "Install" : "Skip");
    draw_property_row(9, 2, state->row, "NVIDIA graphics",
                      system->nvidia_graphics ? "Install nvidia-open-dkms" : "Skip");
    draw_property_row(11, 3, state->row, "Bluetooth",
                      system->bluetooth ? "Install and enable" : "Skip");
    mvaddstr(14, 4, "Hardware choices are explicit so detection cannot silently select a driver.");
}

void handle_hardware(UiState *state, int key)
{
    SystemPlan *system = &state->plan->system;

    if (key == 27) { state->screen = SCREEN_HOME; state->row = 2; return; }
    if (key == KEY_UP && state->row > 0) --state->row;
    else if (key == KEY_DOWN && state->row < 3) ++state->row;
    else if (page_enter_pressed(key)) {
        PackageGroup groups[3];
        size_t count = 0;
        const char *title;

        if (state->row == 0) {
            title = system->laptop
                ? "Pacman packages - Laptop mode"
                : "Pacman packages - Desktop mode";
            if (system->laptop) {
                groups[count++] = PKG_LAPTOP_FIRMWARE;
                groups[count++] = PKG_LAPTOP_TOOLS;
                if (system->desktop == DESKTOP_GNOME) groups[count++] = PKG_GNOME_LAPTOP;
            }
        } else {
            static const PackageGroup hardware_groups[] = {
                PKG_INTEL_GRAPHICS, PKG_NVIDIA_GRAPHICS, PKG_BLUETOOTH
            };
            static const char *const titles[] = {
                "Pacman packages - Intel graphics", "Pacman packages - NVIDIA graphics",
                "Pacman packages - Bluetooth"
            };
            title = titles[state->row - 1];
            groups[count++] = hardware_groups[state->row - 1];
        }
        page_show_packages(state, title, groups, count);
    } else if (key == ' ') {
        bool *values[] = {&system->laptop, &system->intel_graphics,
                          &system->nvidia_graphics, &system->bluetooth};
        *values[state->row] = !*values[state->row];
        state->dirty = true;
    }
}
