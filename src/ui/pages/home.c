#include "private.h"

#include "text.h"

#include <stdio.h>

void draw_home(UiState *state)
{
    static const char *const names[] = {
        "Storage", "Base system", "Hardware", "Desktop & software",
        "Identity", "Review & output", "Exit"
    };
    char details[7][256];
    ValidationReport report;
    char size[32];
    const DiskPlan *root_disk;

    validate_plan(state->plan, &report);
    root_disk = plan_find_disk_for_usage(state->plan, PART_ROOT);
    if (root_disk != NULL) format_size(root_disk->size_bytes, size, sizeof(size));
    else copy_text(size, sizeof(size), "-");
    if (state->plan->storage.disk_count == 0) {
        copy_text(details[0], sizeof(details[0]), "Not configured");
    } else {
        (void)snprintf(details[0], sizeof(details[0]), "%zu disk(s) | root %.100s | %.20s",
                       state->plan->storage.disk_count,
                       root_disk != NULL ? root_disk->path : "not assigned", size);
    }
    (void)snprintf(details[1], sizeof(details[1]), "%s | %s | %s | Secure Boot %s",
                   platform_name(state->plan->system.platform),
                   kernel_name(state->plan->system.kernel),
                   locale_name(state->plan->system.locale),
                   state->plan->system.secure_boot ? "on" : "off");
    (void)snprintf(details[2], sizeof(details[2]), "%s | Intel GPU %s | NVIDIA %s | Bluetooth %s",
                   state->plan->system.laptop ? "laptop" : "desktop",
                   state->plan->system.intel_graphics ? "yes" : "no",
                   state->plan->system.nvidia_graphics ? "yes" : "no",
                   state->plan->system.bluetooth ? "yes" : "no");
    (void)snprintf(details[3], sizeof(details[3]), "%s | recommended packages %s",
                   desktop_name(state->plan->system.desktop),
                   state->plan->system.desktop_recommended ? "yes" : "no");
    (void)snprintf(details[4], sizeof(details[4]), "%.48s@%.63s | %.63s",
                   state->plan->system.username, state->plan->system.hostname,
                   state->plan->system.timezone);
    if (!state->target_identity_matches) {
        copy_text(details[5], sizeof(details[5]), "Target disk identity changed - reselect it");
    } else {
        (void)snprintf(details[5], sizeof(details[5]), "%zu error(s), %zu total issue(s)",
                       report.error_count, report.count);
    }
    copy_text(details[6], sizeof(details[6]), "Leave the builder");

    draw_shell(state, "Installation plan",
               "Up/Down move   Enter open   F2 save   F5 review   F10 quit");
    for (int index = 0; index < 7; ++index) {
        int y = 5 + index * 2;
        if (index == state->row) attron(COLOR_PAIR(COLOR_SELECTED));
        mvprintw(y, 4, " %-21s ", names[index]);
        if (index == state->row) attroff(COLOR_PAIR(COLOR_SELECTED));
        put_clipped(y, 29, COLS - 32, details[index]);
    }
}

void handle_home(UiState *state, int key)
{
    static const Screen screens[] = {
        SCREEN_STORAGE, SCREEN_BASE_SYSTEM, SCREEN_HARDWARE, SCREEN_SOFTWARE,
        SCREEN_IDENTITY, SCREEN_REVIEW, SCREEN_HOME
    };

    if (key == KEY_UP && state->row > 0) --state->row;
    else if (key == KEY_DOWN && state->row < 6) ++state->row;
    else if (page_enter_pressed(key)) {
        if (state->row == 6) quit_builder(state);
        else {
            state->screen = screens[state->row];
            state->row = 0;
        }
    }
}
