#include "private.h"

/* 软件页面只编辑软件包组选项，具体包名仍由 packages.json 提供。 */
void draw_software(UiState *state)
{
    SystemPlan *s = &state->plan->system;
    const char *values[] = {
        desktop_name(s->desktop), s->desktop_recommended ? "Install" : "Skip",
        s->chinese_input ? "Install" : "Skip", s->firewall ? "Install" : "Skip",
        s->printer ? "Install" : "Skip", s->archive_tools ? "Install" : "Skip",
        s->terminal_tools ? "Install" : "Skip", s->extra_tools ? "Install" : "Skip",
        s->desktop_apps ? "Install" : "Skip"
    };
    const char *names[] = {
        "Desktop environment", "Desktop recommended packages", "Chinese input method",
        "Firewall", "Printer support", "Archive tools", "Terminal tools",
        "Additional tools", "Desktop applications"
    };
    draw_shell(state, "Desktop and software groups", "Up/Down move   Enter packages   Space change   Esc back");
    for (int index = 0; index < 9; ++index) draw_property_row(4 + index * 2, index, state->row, names[index], values[index]);
}

void handle_software(UiState *state, int key)
{
    SystemPlan *s = &state->plan->system;
    bool *toggles[] = {&s->desktop_recommended, &s->chinese_input, &s->firewall, &s->printer,
                      &s->archive_tools, &s->terminal_tools, &s->extra_tools, &s->desktop_apps};
    if (key == 27) { state->screen = SCREEN_HOME; state->row = 3; return; }
    if (key == KEY_UP && state->row > 0) --state->row;
    else if (key == KEY_DOWN && state->row < 8) ++state->row;
    else if (page_enter_pressed(key)) {
        PackageGroup group;
        bool available = true;
        static const char *const titles[] = {
            "Pacman packages - Desktop environment",
            "Pacman packages - Desktop recommended",
            "Pacman packages - Chinese input method",
            "Pacman packages - Firewall", "Pacman packages - Printer support",
            "Pacman packages - Archive tools", "Pacman packages - Terminal tools",
            "Pacman packages - Additional tools", "Pacman packages - Desktop applications"
        };

        switch (state->row) {
        case 0:
            available = packages_desktop_group(s->desktop, &group);
            break;
        case 1:
            available = packages_desktop_recommended_group(s->desktop, &group);
            break;
        case 2:
            available = packages_input_group(s->desktop, &group);
            break;
        case 3: group = PKG_FIREWALL; break;
        case 4: group = PKG_PRINTER; break;
        case 5: group = PKG_ARCHIVE_TOOLS; break;
        case 6: group = PKG_TERMINAL_TOOLS; break;
        case 7: group = PKG_EXTRA_TOOLS; break;
        default: group = PKG_DESKTOP_APPS; break;
        }
        page_show_packages(state, titles[state->row], available ? &group : NULL,
                      available ? 1 : 0);
    } else if (key == ' ') {
        if (state->row == 0) s->desktop = (Desktop)(((int)s->desktop + 1) % 4);
        else *toggles[state->row - 1] = !*toggles[state->row - 1];
        state->dirty = true;
    }
}
