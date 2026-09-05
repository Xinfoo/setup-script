#define _POSIX_C_SOURCE 200809L

#include "ui.h"

#include "detect.h"
#include "generator.h"
#include "util.h"

#include <ncurses.h>

#include <ctype.h>
#include <errno.h>
#include <locale.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

typedef enum {
    SCREEN_HOME,
    SCREEN_STORAGE,
    SCREEN_SYSTEM,
    SCREEN_HARDWARE,
    SCREEN_SOFTWARE,
    SCREEN_IDENTITY,
    SCREEN_REVIEW,
    SCREEN_PREVIEW
} Screen;

typedef struct {
    InstallPlan *plan;
    HardwareInventory *inventory;
    const PackageConfig *packages;
    const char *plan_path;
    const char *script_path;
    Screen screen;
    int row;
    int review_offset;
    int preview_offset;
    bool dirty;
    bool target_identity_matches;
    bool running;
    bool quit;
    char status[256];
} UiState;

enum {
    COLOR_TITLE = 1,
    COLOR_SELECTED,
    COLOR_OK,
    COLOR_WARNING,
    COLOR_ERROR,
    COLOR_MUTED
};

static volatile sig_atomic_t stop_requested = 0;
static volatile sig_atomic_t stop_signal = 0;

static void request_stop(int signal_number)
{
    stop_signal = signal_number;
    stop_requested = 1;
}

static void set_status(UiState *state, const char *message)
{
    copy_text(state->status, sizeof(state->status), message);
}

static bool disk_identity_matches(const InstallPlan *plan, const HardwareInventory *inventory)
{
    const DiskInfo *disk;
    if (plan->storage.disk_path[0] == '\0') return true;
    disk = inventory_find_disk(inventory, plan->storage.disk_path);
    if (disk == NULL || disk->read_only || disk->size_bytes != plan->storage.disk_size_bytes) return false;
    if (plan->storage.disk_serial[0] != '\0' &&
        strcmp(disk->serial, plan->storage.disk_serial) != 0) return false;
    if (plan->storage.disk_model[0] != '\0' &&
        strcmp(disk->model, plan->storage.disk_model) != 0) return false;
    if (plan->storage.mode == STORAGE_EXISTING) {
        if (strcasecmp(disk->partition_table, plan->storage.partition_table) != 0) return false;
        for (size_t index = 0; index < plan->storage.partition_count; ++index) {
            const PartitionPlan *planned = &plan->storage.partitions[index];
            const PartitionInfo *current = NULL;
            if (planned->usage == PART_UNUSED) continue;
            for (size_t part = 0; part < disk->partition_count; ++part) {
                if (disk->partitions[part].number == planned->number &&
                    strcmp(disk->partitions[part].path, planned->device) == 0) {
                    current = &disk->partitions[part];
                    break;
                }
            }
            if (current == NULL || current->size_bytes != planned->size_bytes ||
                current->start_sector != planned->start_sector) return false;
            if (planned->part_uuid[0] != '\0' &&
                strcasecmp(current->part_uuid, planned->part_uuid) != 0) return false;
            if (planned->part_type[0] != '\0' &&
                strcasecmp(current->part_type, planned->part_type) != 0) return false;
            if (planned->action == ACTION_KEEP &&
                filesystem_from_name(current->current_fs) !=
                filesystem_from_name(planned->current_fs)) return false;
            if (planned->action == ACTION_KEEP &&
                strcasecmp(current->fs_uuid, planned->fs_uuid) != 0) return false;
        }
    }
    return true;
}

static void init_colors(void)
{
    if (!has_colors()) return;
    start_color();
    use_default_colors();
    init_pair(COLOR_TITLE, COLOR_CYAN, -1);
    init_pair(COLOR_SELECTED, COLOR_BLACK, COLOR_CYAN);
    init_pair(COLOR_OK, COLOR_GREEN, -1);
    init_pair(COLOR_WARNING, COLOR_YELLOW, -1);
    init_pair(COLOR_ERROR, COLOR_RED, -1);
    init_pair(COLOR_MUTED, COLOR_BLUE, -1);
}

static void put_clipped(int y, int x, int width, const char *text)
{
    if (width <= 0 || y < 0 || y >= LINES || x >= COLS) return;
    mvaddnstr(y, x, text != NULL ? text : "", width);
}

static void draw_shell(UiState *state, const char *title, const char *keys)
{
    erase();
    attron(A_BOLD | COLOR_PAIR(COLOR_TITLE));
    put_clipped(0, 2, COLS - 4, "Arch Linux Install Script Builder");
    attroff(A_BOLD | COLOR_PAIR(COLOR_TITLE));
    if (state->dirty) {
        attron(COLOR_PAIR(COLOR_WARNING));
        put_clipped(0, COLS - 13, 11, "[modified]");
        attroff(COLOR_PAIR(COLOR_WARNING));
    }
    mvhline(1, 0, ACS_HLINE, COLS);
    attron(A_BOLD);
    put_clipped(2, 2, COLS - 4, title);
    attroff(A_BOLD);
    mvhline(LINES - 3, 0, ACS_HLINE, COLS);
    attron(COLOR_PAIR(COLOR_MUTED));
    put_clipped(LINES - 2, 1, COLS - 2, keys);
    attroff(COLOR_PAIR(COLOR_MUTED));
    put_clipped(LINES - 1, 1, COLS - 2, state->status);
}

static bool terminal_too_small(void)
{
    if (COLS >= 80 && LINES >= 24) return false;
    erase();
    attron(A_BOLD | COLOR_PAIR(COLOR_ERROR));
    mvprintw(2, 2, "Terminal is too small (%dx%d).", COLS, LINES);
    mvprintw(4, 2, "Resize it to at least 80x24 to continue.");
    attroff(A_BOLD | COLOR_PAIR(COLOR_ERROR));
    refresh();
    return true;
}

static int choose_dialog(const char *title, const char *const options[], size_t count, int current)
{
    int width = COLS - 4;
    int height = (int)count + 6;
    int selected = current >= 0 && (size_t)current < count ? current : 0;
    WINDOW *window;
    if (width > 100) width = 100;
    if (width < 58) width = 58;
    if (height > LINES - 2) height = LINES - 2;
    if (width > COLS - 2) width = COLS - 2;
    window = newwin(height, width, (LINES - height) / 2, (COLS - width) / 2);
    if (window == NULL) return -1;
    keypad(window, TRUE);
    wtimeout(window, 200);
    for (;;) {
        int visible = height - 5;
        int offset = selected >= visible ? selected - visible + 1 : 0;
        werase(window);
        box(window, 0, 0);
        wattron(window, A_BOLD | COLOR_PAIR(COLOR_TITLE));
        mvwaddnstr(window, 1, 2, title, width - 4);
        wattroff(window, A_BOLD | COLOR_PAIR(COLOR_TITLE));
        for (int line = 0; line < visible && (size_t)(offset + line) < count; ++line) {
            int index = offset + line;
            if (index == selected) wattron(window, COLOR_PAIR(COLOR_SELECTED));
            mvwaddnstr(window, line + 3, 2, options[index], width - 4);
            if (index == selected) wattroff(window, COLOR_PAIR(COLOR_SELECTED));
        }
        mvwaddnstr(window, height - 2, 2, "Enter select   Esc cancel", width - 4);
        wrefresh(window);
        int key = wgetch(window);
        if (stop_requested) {
            delwin(window);
            touchwin(stdscr);
            return -1;
        }
        if (key == KEY_RESIZE) {
            delwin(window);
            touchwin(stdscr);
            return -1;
        }
        if (key == ERR) continue;
        if (key == KEY_UP && selected > 0) --selected;
        else if (key == KEY_DOWN && (size_t)(selected + 1) < count) ++selected;
        else if (key == '\n' || key == KEY_ENTER) {
            delwin(window);
            touchwin(stdscr);
            return selected;
        } else if (key == 27 || key == 'q') {
            delwin(window);
            touchwin(stdscr);
            return -1;
        }
    }
}

static bool confirm_dialog(const char *title, const char *message)
{
    const char *const options[] = {"No", "Yes"};
    int width = 68;
    int height = 9;
    WINDOW *window;
    int selected = 0;
    if (width > COLS - 2) width = COLS - 2;
    window = newwin(height, width, (LINES - height) / 2, (COLS - width) / 2);
    if (window == NULL) return false;
    keypad(window, TRUE);
    wtimeout(window, 200);
    for (;;) {
        werase(window);
        box(window, 0, 0);
        wattron(window, A_BOLD | COLOR_PAIR(COLOR_WARNING));
        mvwaddnstr(window, 1, 2, title, width - 4);
        wattroff(window, A_BOLD | COLOR_PAIR(COLOR_WARNING));
        mvwaddnstr(window, 3, 2, message, width - 4);
        for (int index = 0; index < 2; ++index) {
            if (index == selected) wattron(window, COLOR_PAIR(COLOR_SELECTED));
            mvwprintw(window, 5, 22 + index * 12, " %-5s ", options[index]);
            if (index == selected) wattroff(window, COLOR_PAIR(COLOR_SELECTED));
        }
        mvwaddnstr(window, 7, 2, "Left/Right select   Enter confirm   Esc cancel", width - 4);
        wrefresh(window);
        int key = wgetch(window);
        if (stop_requested) {
            delwin(window);
            touchwin(stdscr);
            return false;
        }
        if (key == KEY_RESIZE) {
            delwin(window);
            touchwin(stdscr);
            return false;
        }
        if (key == ERR) continue;
        if (key == KEY_LEFT || key == KEY_RIGHT || key == '\t') selected = 1 - selected;
        else if (key == '\n' || key == KEY_ENTER) {
            delwin(window);
            touchwin(stdscr);
            return selected == 1;
        } else if (key == 27 || key == 'q') {
            delwin(window);
            touchwin(stdscr);
            return false;
        }
    }
}

static bool text_dialog(const char *title, char *value, size_t size)
{
    int width = 68;
    int height = 8;
    WINDOW *window;
    char buffer[AI_PATH_LEN];
    size_t length;
    size_t cursor;
    if (size > sizeof(buffer)) size = sizeof(buffer);
    copy_text(buffer, size, value);
    if (width > COLS - 2) width = COLS - 2;
    window = newwin(height, width, (LINES - height) / 2, (COLS - width) / 2);
    if (window == NULL) return false;
    keypad(window, TRUE);
    wtimeout(window, 200);
    length = strlen(buffer);
    cursor = length;
    curs_set(1);
    for (;;) {
        int field_width = width - 4;
        size_t view_start = cursor >= (size_t)field_width ? cursor - (size_t)field_width + 1 : 0;
        werase(window);
        box(window, 0, 0);
        wattron(window, A_BOLD | COLOR_PAIR(COLOR_TITLE));
        mvwaddnstr(window, 1, 2, title, width - 4);
        wattroff(window, A_BOLD | COLOR_PAIR(COLOR_TITLE));
        mvwhline(window, 3, 2, ' ', field_width);
        mvwaddnstr(window, 3, 2, buffer + view_start, field_width);
        mvwaddnstr(window, 6, 2, "Enter apply   Esc cancel   Ctrl-U clear", width - 4);
        wmove(window, 3, 2 + (int)(cursor - view_start));
        wrefresh(window);
        int key = wgetch(window);
        if (stop_requested) {
            curs_set(0);
            delwin(window);
            touchwin(stdscr);
            return false;
        }
        if (key == KEY_RESIZE) {
            curs_set(0);
            delwin(window);
            touchwin(stdscr);
            return false;
        }
        if (key == ERR) continue;
        if (key == '\n' || key == KEY_ENTER) break;
        if (key == 27) {
            curs_set(0);
            delwin(window);
            touchwin(stdscr);
            return false;
        }
        if ((key == KEY_BACKSPACE || key == 127 || key == '\b') && cursor > 0) {
            memmove(buffer + cursor - 1, buffer + cursor, length - cursor + 1);
            --cursor;
            --length;
        } else if (key == KEY_DC && cursor < length) {
            memmove(buffer + cursor, buffer + cursor + 1, length - cursor);
            --length;
        } else if (key == KEY_LEFT && cursor > 0) {
            --cursor;
        } else if (key == KEY_RIGHT && cursor < length) {
            ++cursor;
        } else if (key == 21) {
            buffer[0] = '\0';
            cursor = length = 0;
        } else if (key >= 0 && key <= 127 && isprint((unsigned char)key) && length + 1 < size) {
            memmove(buffer + cursor + 1, buffer + cursor, length - cursor + 1);
            buffer[cursor++] = (char)key;
            ++length;
        }
    }
    curs_set(0);
    delwin(window);
    touchwin(stdscr);
    if (buffer[0] == '\0') return false;
    copy_text(value, size, buffer);
    return true;
}

static void save_plan(UiState *state)
{
    char error[512] = {0};
    if (plan_save_json(state->plan, state->plan_path, error, sizeof(error))) {
        state->dirty = false;
        (void)snprintf(state->status, sizeof(state->status), "Saved plan to %s", state->plan_path);
    } else {
        (void)snprintf(state->status, sizeof(state->status), "Save failed: %.190s", error);
    }
}

static bool generate(UiState *state)
{
    ValidationReport report;
    char error[512] = {0};
    validate_plan(state->plan, &report);
    if (!state->target_identity_matches) {
        set_status(state, "Cannot generate: target disk identity changed; select the disk again.");
        return false;
    }
    if (report.error_count != 0) {
        (void)snprintf(state->status, sizeof(state->status),
                       "Cannot generate: fix %zu validation error(s).", report.error_count);
        return false;
    }
    if (!plan_save_json(state->plan, state->plan_path, error, sizeof(error))) {
        (void)snprintf(state->status, sizeof(state->status), "Save failed: %.190s", error);
        return false;
    }
    if (!generate_install_script(state->plan, state->packages, state->script_path,
                                 error, sizeof(error))) {
        (void)snprintf(state->status, sizeof(state->status), "Generation failed: %.180s", error);
        return false;
    }
    state->dirty = false;
    (void)snprintf(state->status, sizeof(state->status), "Generated %s", state->script_path);
    return true;
}

static void draw_home(UiState *state)
{
    static const char *const names[] = {
        "Storage", "Base system", "Hardware", "Desktop & software",
        "Identity & boot", "Review & output", "Exit"
    };
    char details[7][256];
    ValidationReport report;
    char size[32];
    validate_plan(state->plan, &report);
    format_size(state->plan->storage.disk_size_bytes, size, sizeof(size));
    if (state->plan->storage.disk_path[0] == '\0')
        copy_text(details[0], sizeof(details[0]), "Not configured");
    else
        (void)snprintf(details[0], sizeof(details[0]), "%.120s | %.20s | %.80s",
                       state->plan->storage.disk_path, size,
                       storage_mode_name(state->plan->storage.mode));
    (void)snprintf(details[1], sizeof(details[1]), "%s | %s | %s",
                   platform_name(state->plan->system.platform), kernel_name(state->plan->system.kernel),
                   state->plan->system.laptop ? "laptop" : "desktop");
    (void)snprintf(details[2], sizeof(details[2]), "Intel GPU %s | NVIDIA %s | Bluetooth %s",
                   state->plan->system.intel_graphics ? "yes" : "no",
                   state->plan->system.nvidia_graphics ? "yes" : "no",
                   state->plan->system.bluetooth ? "yes" : "no");
    (void)snprintf(details[3], sizeof(details[3]), "%s | recommended packages %s",
                   desktop_name(state->plan->system.desktop),
                   state->plan->system.desktop_recommended ? "yes" : "no");
    (void)snprintf(details[4], sizeof(details[4]), "%.48s@%.63s | %.100s | systemd-boot",
                   state->plan->system.username, state->plan->system.hostname,
                   state->plan->system.timezone);
    if (!state->target_identity_matches)
        copy_text(details[5], sizeof(details[5]), "Target disk identity changed - reselect it");
    else
        (void)snprintf(details[5], sizeof(details[5]), "%zu error(s), %zu total issue(s)",
                       report.error_count, report.count);
    copy_text(details[6], sizeof(details[6]), "Leave the builder");

    draw_shell(state, "Installation plan", "Up/Down move   Enter open   F2 save   F5 review   F10 quit");
    for (int index = 0; index < 7; ++index) {
        int y = 5 + index * 2;
        if (index == state->row) attron(COLOR_PAIR(COLOR_SELECTED));
        mvprintw(y, 4, " %-21s ", names[index]);
        if (index == state->row) attroff(COLOR_PAIR(COLOR_SELECTED));
        put_clipped(y, 29, COLS - 32, details[index]);
    }
}

static void handle_home(UiState *state, int key)
{
    if (key == KEY_UP && state->row > 0) --state->row;
    else if (key == KEY_DOWN && state->row < 6) ++state->row;
    else if (key == '\n' || key == KEY_ENTER) {
        static const Screen screens[] = {
            SCREEN_STORAGE, SCREEN_SYSTEM, SCREEN_HARDWARE, SCREEN_SOFTWARE,
            SCREEN_IDENTITY, SCREEN_REVIEW, SCREEN_HOME
        };
        if (state->row == 6) state->quit = confirm_dialog("Quit builder", "Unsaved changes will be lost. Quit now?");
        else {
            state->screen = screens[state->row];
            state->row = 0;
        }
    }
}

static int disk_dialog(const HardwareInventory *inventory, const char *selected_path)
{
    const char *options[AI_MAX_DISKS];
    char labels[AI_MAX_DISKS][256];
    int selected = 0;
    for (size_t index = 0; index < inventory->disk_count; ++index) {
        char size[32];
        char flags[16];
        const DiskInfo *disk = &inventory->disks[index];
        format_size(disk->size_bytes, size, sizeof(size));
        if (!disk->read_only && !disk->removable && !disk->in_use) {
            copy_text(flags, sizeof(flags), "OK");
        } else {
            (void)snprintf(flags, sizeof(flags), "%s%s%s",
                           disk->read_only ? "RO " : "",
                           disk->removable ? "USB " : "",
                           disk->in_use ? "IN-USE" : "");
        }
        (void)snprintf(labels[index], sizeof(labels[index]),
                       "[%-12.12s] %-18.18s %9.9s SN:%-16.16s %.80s",
                       flags, disk->path, size,
                       disk->serial[0] != '\0' ? disk->serial : "-",
                       disk->model[0] != '\0' ? disk->model : "Unknown model");
        options[index] = labels[index];
        if (strcmp(disk->path, selected_path) == 0) selected = (int)index;
    }
    return choose_dialog("Select target disk", options, inventory->disk_count, selected);
}

static void choose_disk(UiState *state)
{
    int choice = disk_dialog(state->inventory, state->plan->storage.disk_path);
    if (choice < 0) return;
    const DiskInfo *disk = &state->inventory->disks[choice];
    if (disk->read_only) {
        set_status(state, "The selected disk is read-only and cannot be an installation target.");
        return;
    }
    if (strcmp(state->plan->storage.disk_path, disk->path) == 0 &&
        disk_identity_matches(state->plan, state->inventory)) {
        set_status(state, "That disk is already the current installation target.");
        return;
    }
    if (disk->removable && !confirm_dialog("Removable disk",
        "This disk is removable or connected by USB. Select it anyway?")) return;
    if (state->plan->storage.disk_path[0] != '\0' &&
        !confirm_dialog("Change target disk", "Changing disk resets the storage plan. Continue?")) return;
    plan_select_disk(state->plan, disk);
    state->target_identity_matches = true;
    state->row = 0;
    state->dirty = true;
    set_status(state, "Target disk selected; existing partitions are untouched.");
}

static void choose_layout(UiState *state)
{
    static const char *const options[] = {
        "Root + recommended swap",
        "100 GiB root + home + recommended swap",
        "Root only, no swap"
    };
    const DiskInfo *disk = inventory_find_disk(state->inventory, state->plan->storage.disk_path);
    int choice;
    if (disk == NULL) {
        set_status(state, "Select a target disk first.");
        return;
    }
    choice = choose_dialog("Guided whole-disk layout (planning only)", options, 3, 0);
    if (choice < 0) return;
    if (!confirm_dialog("Replace partition table",
        "The generated script will erase every partition on this disk. Continue planning?")) return;
    plan_use_automatic(state->plan, disk, choice == 0 ? STORAGE_AUTO_ROOT_SWAP :
                       choice == 1 ? STORAGE_AUTO_HOME_SWAP : STORAGE_AUTO_ROOT_ONLY);
    state->row = 0;
    state->dirty = true;
    set_status(state, "Guided layout applied to the plan; no disk was modified.");
}

static void use_existing(UiState *state)
{
    const DiskInfo *disk = inventory_find_disk(state->inventory, state->plan->storage.disk_path);
    if (disk == NULL) {
        set_status(state, "Select a target disk first.");
        return;
    }
    if (!confirm_dialog("Use existing partitions", "Reset current storage assignments from detected partitions?")) return;
    plan_select_disk(state->plan, disk);
    state->row = 0;
    state->dirty = true;
    state->target_identity_matches = true;
    set_status(state, "Existing partitions loaded. KEEP never formats a filesystem.");
}

static void refresh_disks(UiState *state)
{
    HardwareInventory *inventory = calloc(1, sizeof(*inventory));
    char error[512] = {0};
    if (inventory == NULL) {
        set_status(state, "Refresh failed: out of memory.");
        return;
    }
    if (!detect_hardware(inventory, error, sizeof(error))) {
        (void)snprintf(state->status, sizeof(state->status), "Refresh failed: %.190s", error);
        free(inventory);
        return;
    }
    *state->inventory = *inventory;
    free(inventory);
    state->target_identity_matches = disk_identity_matches(state->plan, state->inventory);
    if (!state->target_identity_matches) {
        set_status(state, "Warning: target path now refers to a missing or different disk.");
    } else {
        set_status(state, "Block device inventory refreshed.");
    }
}

static void draw_storage(UiState *state)
{
    StoragePlan *storage = &state->plan->storage;
    char disk_size[32];
    draw_shell(state, "Storage plan - editing changes only the generated plan",
               "D disk   A guided layout   E existing   U purpose   F format   O F2FS profile   R refresh   Esc back");
    if (storage->disk_path[0] == '\0') {
        attron(COLOR_PAIR(COLOR_WARNING));
        mvaddstr(5, 4, "No target disk selected.");
        attroff(COLOR_PAIR(COLOR_WARNING));
        mvaddstr(7, 4, "Press D to select a disk. This program does not modify partition tables.");
        return;
    }
    format_size(storage->disk_size_bytes, disk_size, sizeof(disk_size));
    mvprintw(4, 2, "Disk: %s  %-28.28s  %s", storage->disk_path, storage->disk_model, disk_size);
    mvprintw(5, 2, "Mode: %s", storage_mode_name(storage->mode));
    if (storage->mode != STORAGE_EXISTING) {
        attron(A_BOLD | COLOR_PAIR(COLOR_ERROR));
        put_clipped(6, 2, COLS - 4, "DESTRUCTIVE PLAN: generated script will replace the GPT partition table.");
        attroff(A_BOLD | COLOR_PAIR(COLOR_ERROR));
    } else {
        attron(COLOR_PAIR(COLOR_OK));
        put_clipped(6, 2, COLS - 4, "KEEP skips mkfs but mounted files may be updated; FORMAT erases the partition.");
        attroff(COLOR_PAIR(COLOR_OK));
    }
    attron(A_BOLD);
    mvprintw(8, 2, "%-18s %8s %-8s %-8s %-7s %-7s %-9s",
             "Device", "Size", "Current", "Action", "Target", "Purpose", "Mount");
    if (COLS >= 96) addstr(" Profile");
    attroff(A_BOLD);
    int available = LINES - 13;
    int offset = state->row >= available ? state->row - available + 1 : 0;
    for (int line = 0; line < available; ++line) {
        int index = offset + line;
        char size[32];
        char operation[16];
        const PartitionPlan *part;
        if ((size_t)index >= storage->partition_count) break;
        part = &storage->partitions[index];
        format_size(part->size_bytes, size, sizeof(size));
        if (part->usage == PART_UNUSED) copy_text(operation, sizeof(operation), "IGNORE");
        else if (part->planned) copy_text(operation, sizeof(operation), "CREATE");
        else copy_text(operation, sizeof(operation), part->action == ACTION_FORMAT ? "FORMAT" : "KEEP");
        if (index == state->row) attron(COLOR_PAIR(COLOR_SELECTED));
        mvprintw(9 + line, 2, "%-18.18s %8.8s %-8.8s %-8.8s %-7.7s %-7.7s %-9.9s",
                 part->device, size, part->current_fs[0] != '\0' ? part->current_fs : "-", operation,
                 part->action == ACTION_FORMAT ? filesystem_name(part->target_fs) :
                 (part->current_fs[0] != '\0' ? part->current_fs : "-"),
                 usage_name(part->usage), partition_mountpoint(part->usage));
        if (COLS >= 96) {
            Filesystem effective = part->action == ACTION_FORMAT ? part->target_fs :
                                   filesystem_from_name(part->current_fs);
            printw(" %s", effective == FS_F2FS ? f2fs_mode_name(part->f2fs_mode) : "-");
        }
        if (index == state->row) attroff(COLOR_PAIR(COLOR_SELECTED));
    }
}

static bool regular_filesystem(Filesystem filesystem)
{
    return filesystem == FS_EXT4 || filesystem == FS_XFS || filesystem == FS_F2FS;
}

static bool edit_partition_format(UiState *state, PartitionPlan *partition);

static void edit_partition_usage(UiState *state, PartitionPlan *partition)
{
    static const char *const options[] = {
        "Ignore this partition",
        "Root filesystem                         /",
        "EFI system partition                    /boot",
        "User data                               /home",
        "Variable data                           /var",
        "System files                            /usr",
        "Optional software                       /opt",
        "Swap"
    };
    PartitionUsage old_usage = partition->usage;
    PartitionAction old_action = partition->action;
    Filesystem old_target = partition->target_fs;
    PartitionPlan original = *partition;
    Filesystem current;
    int choice;

    if (partition->planned) {
        set_status(state, "Guided-layout purposes are fixed; choose another layout to change them.");
        return;
    }
    choice = choose_dialog("Assign partition purpose", options,
                           sizeof(options) / sizeof(options[0]), (int)partition->usage);
    if (choice < 0) return;

    partition->usage = (PartitionUsage)choice;
    current = filesystem_from_name(partition->current_fs);
    if (partition->usage == PART_UNUSED) {
        partition->action = ACTION_KEEP;
        partition->target_fs = FS_NONE;
    } else if (partition->usage == PART_ROOT || partition->usage == PART_VAR ||
               partition->usage == PART_USR) {
        partition->action = ACTION_FORMAT;
        partition->target_fs = regular_filesystem(current) ? current : FS_EXT4;
    } else if (partition->usage == PART_BOOT) {
        partition->action = current == FS_VFAT ? ACTION_KEEP : ACTION_FORMAT;
        partition->target_fs = current == FS_VFAT ? FS_NONE : FS_VFAT;
    } else if (partition->usage == PART_SWAP) {
        partition->action = current == FS_SWAP ? ACTION_KEEP : ACTION_FORMAT;
        partition->target_fs = current == FS_SWAP ? FS_NONE : FS_SWAP;
    } else if (regular_filesystem(current)) {
        partition->action = ACTION_KEEP;
        partition->target_fs = FS_NONE;
    } else {
        partition->action = ACTION_FORMAT;
        partition->target_fs = FS_EXT4;
    }

    if (partition->usage != PART_UNUSED && partition->usage != old_usage &&
        !edit_partition_format(state, partition)) {
        *partition = original;
        set_status(state, "Purpose change canceled; no partition action was changed.");
        return;
    }

    if (partition->usage != old_usage || partition->action != old_action ||
        partition->target_fs != old_target) {
        state->dirty = true;
        if (partition->usage == PART_UNUSED) {
            (void)snprintf(state->status, sizeof(state->status),
                           "%.180s is ignored and will not be mounted or formatted.",
                           partition->device);
        }
    }
}

static bool edit_partition_format(UiState *state, PartitionPlan *partition)
{
    const char *options[4];
    PartitionAction actions[4];
    Filesystem filesystems[4];
    char keep_label[96];
    size_t count = 0;
    int current_choice = 0;
    int choice;
    Filesystem current = filesystem_from_name(partition->current_fs);

    if (partition->usage == PART_UNUSED) {
        set_status(state, "Assign a purpose before choosing a filesystem action.");
        return false;
    }
    if (partition->planned &&
        (partition->usage == PART_BOOT || partition->usage == PART_SWAP)) {
        set_status(state, partition->usage == PART_BOOT ?
                   "Guided EFI partitions are always created as FAT32." :
                   "Guided swap partitions are always created with mkswap.");
        return false;
    }

    if (!partition->planned && partition->usage != PART_ROOT &&
        partition->usage != PART_VAR && partition->usage != PART_USR &&
        ((partition->usage == PART_BOOT && current == FS_VFAT) ||
         (partition->usage == PART_SWAP && current == FS_SWAP) ||
         (partition->usage != PART_BOOT && partition->usage != PART_SWAP &&
          regular_filesystem(current)))) {
        (void)snprintf(keep_label, sizeof(keep_label),
                       "KEEP current %s filesystem (no formatting)",
                       filesystem_name(current));
        options[count] = keep_label;
        actions[count] = ACTION_KEEP;
        filesystems[count++] = FS_NONE;
    }

    if (partition->usage == PART_BOOT) {
        options[count] = "FORMAT as FAT32 (erases this partition)";
        actions[count] = ACTION_FORMAT;
        filesystems[count++] = FS_VFAT;
    } else if (partition->usage == PART_SWAP) {
        options[count] = "FORMAT as swap (erases this partition)";
        actions[count] = ACTION_FORMAT;
        filesystems[count++] = FS_SWAP;
    } else {
        options[count] = "FORMAT as ext4 (erases this partition)";
        actions[count] = ACTION_FORMAT;
        filesystems[count++] = FS_EXT4;
        options[count] = "FORMAT as XFS (erases this partition)";
        actions[count] = ACTION_FORMAT;
        filesystems[count++] = FS_XFS;
        options[count] = "FORMAT as F2FS (erases this partition)";
        actions[count] = ACTION_FORMAT;
        filesystems[count++] = FS_F2FS;
    }

    for (size_t index = 0; index < count; ++index) {
        if (actions[index] == partition->action &&
            (actions[index] == ACTION_KEEP || filesystems[index] == partition->target_fs)) {
            current_choice = (int)index;
            break;
        }
    }
    choice = choose_dialog("Filesystem action", options, count, current_choice);
    if (choice < 0) return false;
    if (partition->action != actions[choice] || partition->target_fs != filesystems[choice]) {
        partition->action = actions[choice];
        partition->target_fs = filesystems[choice];
        state->dirty = true;
    }
    if (partition->action == ACTION_KEEP) {
        partition->f2fs_mode = F2FS_DEFAULT;
        (void)snprintf(state->status, sizeof(state->status),
                       "%.180s will be kept without formatting.", partition->device);
    } else {
        (void)snprintf(state->status, sizeof(state->status),
                       "%.180s will be formatted as %s.", partition->device,
                       filesystem_name(partition->target_fs));
    }
    return true;
}

static void edit_f2fs_mode(UiState *state, PartitionPlan *partition)
{
    static const char *const options[] = {
        "Default mount options",
        "Balanced: noatime, lazytime, GC tuning",
        "Compressed: balanced options plus zstd compression"
    };
    Filesystem effective = partition->action == ACTION_FORMAT ? partition->target_fs :
                           filesystem_from_name(partition->current_fs);
    int choice;

    if (effective != FS_F2FS || partition->usage == PART_UNUSED ||
        partition->action != ACTION_FORMAT) {
        set_status(state, "Mount profiles apply only when formatting an F2FS partition.");
        return;
    }
    choice = choose_dialog("F2FS mount profile", options,
                           sizeof(options) / sizeof(options[0]), (int)partition->f2fs_mode);
    if (choice < 0) return;
    if (partition->f2fs_mode != (F2fsMountMode)choice) {
        partition->f2fs_mode = (F2fsMountMode)choice;
        state->dirty = true;
    }
    (void)snprintf(state->status, sizeof(state->status), "F2FS mount profile: %s",
                   f2fs_mode_name(partition->f2fs_mode));
}

static void handle_storage(UiState *state, int key)
{
    StoragePlan *storage = &state->plan->storage;
    if (key == 27) { state->screen = SCREEN_HOME; state->row = 0; return; }
    if (key == 'd' || key == 'D') { choose_disk(state); return; }
    if (key == 'a' || key == 'A') { choose_layout(state); return; }
    if (key == 'e' || key == 'E') { use_existing(state); return; }
    if (key == 'r' || key == 'R') { refresh_disks(state); return; }
    if (storage->partition_count == 0) return;
    if (key == KEY_UP && state->row > 0) --state->row;
    else if (key == KEY_DOWN && (size_t)(state->row + 1) < storage->partition_count) ++state->row;
    else if (key == 'u' || key == 'U' || key == ' ') {
        edit_partition_usage(state, &storage->partitions[state->row]);
    } else if (key == 'f' || key == 'F' || key == '\n' || key == KEY_ENTER) {
        edit_partition_format(state, &storage->partitions[state->row]);
    } else if (key == 'o' || key == 'O') {
        edit_f2fs_mode(state, &storage->partitions[state->row]);
    }
}

static void property_row(int y, int index, int selected, const char *name, const char *value)
{
    if (index == selected) attron(COLOR_PAIR(COLOR_SELECTED));
    mvprintw(y, 4, " %-28s ", name);
    if (index == selected) attroff(COLOR_PAIR(COLOR_SELECTED));
    put_clipped(y, 36, COLS - 39, value);
}

static void draw_system(UiState *state)
{
    SystemPlan *system = &state->plan->system;
    draw_shell(state, "Base system", "Up/Down move   Enter/Space change   Esc back");
    property_row(5, 0, state->row, "CPU platform", platform_name(system->platform));
    property_row(7, 1, state->row, "Kernel", kernel_name(system->kernel));
    property_row(9, 2, state->row, "Device type", system->laptop ? "Laptop (TLP enabled)" : "Desktop");
    property_row(11, 3, state->row, "Default locale", locale_name(system->locale));
    property_row(13, 4, state->row, "Package source", system->local_mirror ? "Local F2FS-DATA mirror" : "Network mirror");
    property_row(15, 5, state->row, "Installed system mirrors", system->china_mirrors ? "China mirror list" : "Keep current mirror list");
}

static void handle_system(UiState *state, int key)
{
    SystemPlan *system = &state->plan->system;
    if (key == 27) { state->screen = SCREEN_HOME; state->row = 1; return; }
    if (key == KEY_UP && state->row > 0) --state->row;
    else if (key == KEY_DOWN && state->row < 5) ++state->row;
    else if (key == ' ' || key == '\n' || key == KEY_ENTER || key == KEY_LEFT || key == KEY_RIGHT) {
        switch (state->row) {
        case 0: system->platform = (Platform)(((int)system->platform + 1) % 3); break;
        case 1: system->kernel = (Kernel)(((int)system->kernel + 1) % 4); break;
        case 2: system->laptop = !system->laptop; break;
        case 3: system->locale = system->locale == LOCALE_EN_US ? LOCALE_ZH_CN : LOCALE_EN_US; break;
        case 4: system->local_mirror = !system->local_mirror; break;
        case 5: system->china_mirrors = !system->china_mirrors; break;
        }
        state->dirty = true;
    }
}

static void draw_hardware(UiState *state)
{
    SystemPlan *system = &state->plan->system;
    draw_shell(state, "Hardware support", "Up/Down move   Enter/Space toggle   Esc back");
    property_row(6, 0, state->row, "Intel integrated graphics", system->intel_graphics ? "Install" : "Skip");
    property_row(8, 1, state->row, "NVIDIA graphics", system->nvidia_graphics ? "Install nvidia-open-dkms" : "Skip");
    property_row(10, 2, state->row, "Bluetooth", system->bluetooth ? "Install and enable" : "Skip");
    mvaddstr(13, 4, "Hardware choices are explicit so detection can never silently install a wrong driver.");
}

static void handle_hardware(UiState *state, int key)
{
    bool *values[] = {&state->plan->system.intel_graphics, &state->plan->system.nvidia_graphics,
                     &state->plan->system.bluetooth};
    if (key == 27) { state->screen = SCREEN_HOME; state->row = 2; return; }
    if (key == KEY_UP && state->row > 0) --state->row;
    else if (key == KEY_DOWN && state->row < 2) ++state->row;
    else if (key == ' ' || key == '\n' || key == KEY_ENTER) {
        *values[state->row] = !*values[state->row];
        state->dirty = true;
    }
}

static void draw_software(UiState *state)
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
    draw_shell(state, "Desktop and software groups", "Up/Down move   Enter/Space change   Esc back");
    for (int index = 0; index < 9; ++index) property_row(4 + index * 2, index, state->row, names[index], values[index]);
}

static void handle_software(UiState *state, int key)
{
    SystemPlan *s = &state->plan->system;
    bool *toggles[] = {&s->desktop_recommended, &s->chinese_input, &s->firewall, &s->printer,
                      &s->archive_tools, &s->terminal_tools, &s->extra_tools, &s->desktop_apps};
    if (key == 27) { state->screen = SCREEN_HOME; state->row = 3; return; }
    if (key == KEY_UP && state->row > 0) --state->row;
    else if (key == KEY_DOWN && state->row < 8) ++state->row;
    else if (key == ' ' || key == '\n' || key == KEY_ENTER) {
        if (state->row == 0) s->desktop = (Desktop)(((int)s->desktop + 1) % 4);
        else *toggles[state->row - 1] = !*toggles[state->row - 1];
        state->dirty = true;
    }
}

static void draw_identity(UiState *state)
{
    SystemPlan *s = &state->plan->system;
    draw_shell(state, "Identity and boot", "Up/Down move   Enter edit/toggle   Esc back");
    property_row(5, 0, state->row, "Hostname", s->hostname);
    property_row(7, 1, state->row, "Username", s->username);
    property_row(9, 2, state->row, "Timezone", s->timezone);
    property_row(11, 3, state->row, "Root password", "Prompt during installation");
    property_row(13, 4, state->row, "User password", "Prompt during installation");
    property_row(15, 5, state->row, "Bootloader", "systemd-boot");
    property_row(17, 6, state->row, "Create EFI NVRAM entry", s->create_efi_entry ? "Yes" : "No");
    property_row(19, 7, state->row, "Shim/MOK (kernel only)", s->secure_boot ? "Enabled" : "Disabled");
}

static void handle_identity(UiState *state, int key)
{
    SystemPlan *s = &state->plan->system;
    if (key == 27) { state->screen = SCREEN_HOME; state->row = 4; return; }
    if (key == KEY_UP && state->row > 0) --state->row;
    else if (key == KEY_DOWN && state->row < 7) ++state->row;
    else if (key == ' ' || key == '\n' || key == KEY_ENTER) {
        bool changed = false;
        if (state->row == 0) changed = text_dialog("Hostname", s->hostname, sizeof(s->hostname));
        else if (state->row == 1) changed = text_dialog("Username", s->username, sizeof(s->username));
        else if (state->row == 2) changed = text_dialog("Timezone (for example Asia/Shanghai)", s->timezone, sizeof(s->timezone));
        else if (state->row == 6) { s->create_efi_entry = !s->create_efi_entry; changed = true; }
        else if (state->row == 7) { s->secure_boot = !s->secure_boot; changed = true; }
        if (changed) state->dirty = true;
    }
}

static void draw_review(UiState *state)
{
    ValidationReport report;
    StoragePlan *storage = &state->plan->storage;
    char lines[AI_MAX_PARTITIONS + AI_MAX_ISSUES + 20][256];
    int colors[AI_MAX_PARTITIONS + AI_MAX_ISSUES + 20] = {0};
    size_t count = 0;
    int y = 4;
    validate_plan(state->plan, &report);
    draw_shell(state, "Review and output", "Up/Down/PgUp/PgDn scroll   P preview   G generate   X generate & run   Esc back");
    if (report.error_count == 0 && state->target_identity_matches) {
        (void)snprintf(lines[count], sizeof(lines[count]), "Plan is ready (%zu warning(s)).", report.count);
        colors[count++] = COLOR_OK;
    } else {
        (void)snprintf(lines[count], sizeof(lines[count]), "Plan has %zu blocking error(s)%s.",
                       report.error_count,
                       state->target_identity_matches ? "" : " plus a target identity mismatch");
        colors[count++] = COLOR_ERROR;
    }
    lines[count++][0] = '\0';
    copy_text(lines[count], sizeof(lines[count]), "DESTRUCTIVE AND STORAGE OPERATIONS");
    colors[count++] = COLOR_TITLE;
    if (storage->mode != STORAGE_EXISTING) {
        (void)snprintf(lines[count], sizeof(lines[count]), "ERASE   %.120s (%.100s)",
                       storage->disk_path, storage->disk_model);
        colors[count++] = COLOR_ERROR;
    }
    for (size_t index = 0; index < storage->partition_count; ++index) {
        const PartitionPlan *part = &storage->partitions[index];
        if (part->usage == PART_UNUSED || count >= sizeof(lines) / sizeof(lines[0])) continue;
        (void)snprintf(lines[count], sizeof(lines[count]), "%-7s %-20.120s -> %-8.8s %.40s",
                       part->action == ACTION_FORMAT ? (part->planned ? "CREATE" : "FORMAT") : "KEEP",
                       part->device,
                       part->action == ACTION_FORMAT ? filesystem_name(part->target_fs) : part->current_fs,
                       partition_mountpoint(part->usage));
        colors[count++] = part->action == ACTION_FORMAT ? COLOR_WARNING : 0;
    }
    lines[count++][0] = '\0';
    copy_text(lines[count], sizeof(lines[count]), "SYSTEM");
    colors[count++] = COLOR_TITLE;
    (void)snprintf(lines[count++], sizeof(lines[0]), "%s | %s | %s | %s",
                   platform_name(state->plan->system.platform), kernel_name(state->plan->system.kernel),
                   desktop_name(state->plan->system.desktop), locale_name(state->plan->system.locale));
    (void)snprintf(lines[count++], sizeof(lines[0]), "%.48s@%.63s | %.100s | Secure Boot %s",
                   state->plan->system.username, state->plan->system.hostname,
                   state->plan->system.timezone, state->plan->system.secure_boot ? "on" : "off");
    lines[count++][0] = '\0';
    copy_text(lines[count], sizeof(lines[count]), "VALIDATION");
    colors[count++] = COLOR_TITLE;
    if (!state->target_identity_matches) {
        copy_text(lines[count], sizeof(lines[count]),
                  "ERROR Target disk identity no longer matches the saved plan.");
        colors[count++] = COLOR_ERROR;
    }
    for (size_t index = 0; index < report.count && count < sizeof(lines) / sizeof(lines[0]); ++index) {
        (void)snprintf(lines[count], sizeof(lines[count]), "%s %.240s",
                       report.issues[index].severity == ISSUE_ERROR ? "ERROR" : "WARN ",
                       report.issues[index].message);
        colors[count++] = report.issues[index].severity == ISSUE_ERROR ? COLOR_ERROR : COLOR_WARNING;
    }
    if (report.count == 0 && state->target_identity_matches) {
        copy_text(lines[count], sizeof(lines[count]), "No validation issues.");
        colors[count++] = COLOR_OK;
    }
    {
        int page = LINES - 8;
        int maximum = count > (size_t)page ? (int)count - page : 0;
        if (state->review_offset > maximum) state->review_offset = maximum;
        for (int line = 0; line < page && (size_t)(state->review_offset + line) < count; ++line) {
            size_t index = (size_t)(state->review_offset + line);
            if (colors[index] != 0) attron(COLOR_PAIR(colors[index]));
            if (colors[index] == COLOR_TITLE || index == 0) attron(A_BOLD);
            put_clipped(y++, 2, COLS - 4, lines[index]);
            if (colors[index] == COLOR_TITLE || index == 0) attroff(A_BOLD);
            if (colors[index] != 0) attroff(COLOR_PAIR(colors[index]));
        }
        if (maximum > 0) mvprintw(LINES - 4, COLS - 22, "lines %d-%d of %zu",
                                  state->review_offset + 1,
                                  state->review_offset + page < (int)count ?
                                  state->review_offset + page : (int)count, count);
    }
}

static void handle_review(UiState *state, int key)
{
    if (key == 27) { state->screen = SCREEN_HOME; state->row = 5; return; }
    if (key == KEY_UP && state->review_offset > 0) --state->review_offset;
    else if (key == KEY_DOWN) ++state->review_offset;
    else if (key == KEY_PPAGE) {
        state->review_offset -= 10;
        if (state->review_offset < 0) state->review_offset = 0;
    } else if (key == KEY_NPAGE) state->review_offset += 10;
    else if (key == 'g' || key == 'G') (void)generate(state);
    else if (key == 'p' || key == 'P') {
        if (generate(state)) { state->screen = SCREEN_PREVIEW; state->preview_offset = 0; }
    } else if (key == 'x' || key == 'X') {
        if (generate(state) && confirm_dialog("Run generated installer",
            "Leave the TUI and execute the generated Bash script now?")) {
            state->running = true;
            state->quit = true;
        }
    }
}

static void draw_preview(UiState *state)
{
    FILE *file;
    char *line = NULL;
    size_t capacity = 0;
    ssize_t length;
    int number = 0;
    int y = 4;
    draw_shell(state, "Generated Bash preview", "Up/Down scroll   G regenerate   Esc back");
    file = fopen(state->script_path, "r");
    if (file == NULL) {
        mvprintw(5, 2, "Cannot open %s: %s", state->script_path, strerror(errno));
        return;
    }
    while ((length = getline(&line, &capacity, file)) >= 0) {
        if (number++ < state->preview_offset) continue;
        if (y >= LINES - 3) break;
        if (length > 0 && line[length - 1] == '\n') line[length - 1] = '\0';
        mvprintw(y, 1, "%4d ", number);
        put_clipped(y++, 7, COLS - 8, line);
    }
    free(line);
    (void)fclose(file);
}

static void handle_preview(UiState *state, int key)
{
    if (key == 27) { state->screen = SCREEN_REVIEW; return; }
    if ((key == KEY_UP || key == KEY_PPAGE) && state->preview_offset > 0)
        state->preview_offset -= key == KEY_PPAGE ? 10 : 1;
    else if (key == KEY_DOWN) ++state->preview_offset;
    else if (key == KEY_NPAGE) state->preview_offset += 10;
    else if (key == 'g' || key == 'G') (void)generate(state);
    if (state->preview_offset < 0) state->preview_offset = 0;
}

static void draw_current(UiState *state)
{
    if (terminal_too_small()) return;
    switch (state->screen) {
    case SCREEN_HOME: draw_home(state); break;
    case SCREEN_STORAGE: draw_storage(state); break;
    case SCREEN_SYSTEM: draw_system(state); break;
    case SCREEN_HARDWARE: draw_hardware(state); break;
    case SCREEN_SOFTWARE: draw_software(state); break;
    case SCREEN_IDENTITY: draw_identity(state); break;
    case SCREEN_REVIEW: draw_review(state); break;
    case SCREEN_PREVIEW: draw_preview(state); break;
    }
    refresh();
}

static void handle_key(UiState *state, int key)
{
    if (key == KEY_F(2)) { save_plan(state); return; }
    if (key == KEY_F(5)) { state->screen = SCREEN_REVIEW; state->row = 0; return; }
    if (key == KEY_F(10)) {
        state->quit = !state->dirty || confirm_dialog("Quit builder", "Unsaved changes will be lost. Quit now?");
        return;
    }
    switch (state->screen) {
    case SCREEN_HOME: handle_home(state, key); break;
    case SCREEN_STORAGE: handle_storage(state, key); break;
    case SCREEN_SYSTEM: handle_system(state, key); break;
    case SCREEN_HARDWARE: handle_hardware(state, key); break;
    case SCREEN_SOFTWARE: handle_software(state, key); break;
    case SCREEN_IDENTITY: handle_identity(state, key); break;
    case SCREEN_REVIEW: handle_review(state, key); break;
    case SCREEN_PREVIEW: handle_preview(state, key); break;
    }
}

int run_tui(InstallPlan *plan, HardwareInventory *inventory,
            const PackageConfig *packages, const char *plan_path,
            const char *script_path)
{
    struct sigaction action;
    UiState state = {
        .plan = plan,
        .inventory = inventory,
        .packages = packages,
        .plan_path = plan_path,
        .script_path = script_path,
        .screen = SCREEN_HOME,
        .target_identity_matches = disk_identity_matches(plan, inventory)
    };
    (void)setlocale(LC_ALL, "");
    if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO)) {
        (void)fprintf(stderr, "The TUI requires an interactive terminal.\n");
        return EXIT_FAILURE;
    }
    if (initscr() == NULL) {
        (void)fprintf(stderr, "Cannot initialize ncurses.\n");
        return EXIT_FAILURE;
    }
    memset(&action, 0, sizeof(action));
    action.sa_handler = request_stop;
    sigemptyset(&action.sa_mask);
    (void)sigaction(SIGINT, &action, NULL);
    (void)sigaction(SIGTERM, &action, NULL);
    (void)sigaction(SIGHUP, &action, NULL);
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    timeout(200);
    curs_set(0);
    init_colors();
    set_status(&state, "No system changes are made until a generated script is explicitly run.");
    while (!state.quit && !stop_requested) {
        draw_current(&state);
        int key = getch();
        if (stop_requested) break;
        if (key == ERR) continue;
        if (terminal_too_small()) {
            continue;
        }
        handle_key(&state, key);
    }
    endwin();
    if (state.running && !stop_requested) {
        execl("/usr/bin/bash", "bash", "--", script_path, (char *)NULL);
        (void)fprintf(stderr, "Cannot execute %s: %s\n", script_path, strerror(errno));
        return EXIT_FAILURE;
    }
    if (stop_requested) return 128 + (int)stop_signal;
    return EXIT_SUCCESS;
}
