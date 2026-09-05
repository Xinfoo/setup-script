#define _POSIX_C_SOURCE 200809L

#include "private.h"

#include "util.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

/* 首页摘要使用包含根分区的磁盘作为主要安装目标显示。 */
static const DiskPlan *root_disk_plan(const InstallPlan *plan)
{
    for (size_t disk_index = 0; disk_index < plan->storage.disk_count; ++disk_index) {
        const DiskPlan *disk = &plan->storage.disks[disk_index];
        for (size_t index = 0; index < disk->partition_count; ++index) {
            if (disk->partitions[index].usage == PART_ROOT) return disk;
        }
    }
    return NULL;
}

void draw_home(UiState *state)
{
    static const char *const names[] = {
        "Storage", "Base system", "Hardware", "Desktop & software",
        "Identity & boot", "Review & output", "Exit"
    };
    char details[7][256];
    ValidationReport report;
    char size[32];
    validate_plan(state->plan, &report);
    const DiskPlan *root_disk = root_disk_plan(state->plan);
    if (root_disk != NULL) format_size(root_disk->size_bytes, size, sizeof(size));
    else copy_text(size, sizeof(size), "-");
    if (state->plan->storage.disk_count == 0)
        copy_text(details[0], sizeof(details[0]), "Not configured");
    else
        (void)snprintf(details[0], sizeof(details[0]), "%zu disk(s) | root %.100s | %.20s",
                       state->plan->storage.disk_count,
                       root_disk != NULL ? root_disk->path : "not assigned", size);
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

void handle_home(UiState *state, int key)
{
    if (key == KEY_UP && state->row > 0) --state->row;
    else if (key == KEY_DOWN && state->row < 6) ++state->row;
    else if (key == '\n' || key == KEY_ENTER) {
        static const Screen screens[] = {
            SCREEN_STORAGE, SCREEN_SYSTEM, SCREEN_HARDWARE, SCREEN_SOFTWARE,
            SCREEN_IDENTITY, SCREEN_REVIEW, SCREEN_HOME
        };
        if (state->row == 6) quit_builder(state);
        else {
            state->screen = screens[state->row];
            state->row = 0;
        }
    }
}

/* 属性型页面共用同一行布局：左侧可选字段，右侧显示当前值。 */
static void property_row(int y, int index, int selected, const char *name, const char *value)
{
    if (index == selected) attron(COLOR_PAIR(COLOR_SELECTED));
    mvprintw(y, 4, " %-28s ", name);
    if (index == selected) attroff(COLOR_PAIR(COLOR_SELECTED));
    put_clipped(y, 36, COLS - 39, value);
}

static bool enter_pressed(int key)
{
    return key == '\n' || key == KEY_ENTER;
}

/* 软件包弹窗只接收组编号，实际名称始终来自本次加载的 packages.json。 */
static void show_packages(UiState *state, const char *title,
                          const PackageGroup groups[], size_t group_count)
{
    packages_dialog(title, state->packages, groups, group_count);
}

static PackageGroup kernel_package_group(Kernel kernel)
{
    switch (kernel) {
    case KERNEL_LINUX: return PKG_KERNEL_LINUX;
    case KERNEL_LTS: return PKG_KERNEL_LTS;
    case KERNEL_ZEN: return PKG_KERNEL_ZEN;
    case KERNEL_HARDENED: return PKG_KERNEL_HARDENED;
    }
    return PKG_KERNEL_LINUX;
}

/* 基础系统前三项会改变软件包集合，Enter 展示包名，Space 保留原修改动作。 */
static bool inspect_system_packages(UiState *state)
{
    SystemPlan *system = &state->plan->system;
    PackageGroup groups[3];
    size_t count = 0;
    const char *title;

    if (state->row == 0) {
        title = "Pacman packages - CPU platform";
        if (system->platform == PLATFORM_INTEL) groups[count++] = PKG_PLATFORM_INTEL;
        else if (system->platform == PLATFORM_AMD) groups[count++] = PKG_PLATFORM_AMD;
    } else if (state->row == 1) {
        title = "Pacman packages - selected kernel";
        groups[count++] = kernel_package_group(system->kernel);
    } else if (state->row == 2) {
        title = system->laptop
            ? "Pacman packages - Laptop mode"
            : "Pacman packages - Desktop mode";
        if (system->laptop) {
            groups[count++] = PKG_LAPTOP_FIRMWARE;
            groups[count++] = PKG_LAPTOP_TOOLS;
            if (system->desktop == DESKTOP_GNOME) groups[count++] = PKG_GNOME_LAPTOP;
        }
    } else {
        return false;
    }
    show_packages(state, title, groups, count);
    return true;
}

/* 基础系统页面：平台、内核、设备类型、区域和软件源。 */
void draw_system(UiState *state)
{
    SystemPlan *system = &state->plan->system;
    const char *keys = state->row <= 2
        ? "Up/Down move   Enter packages   Space/Left/Right change   Esc back"
        : "Up/Down move   Enter/Space/Left/Right change   Esc back";
    draw_shell(state, "Base system", keys);
    property_row(5, 0, state->row, "CPU platform", platform_name(system->platform));
    property_row(7, 1, state->row, "Kernel", kernel_name(system->kernel));
    property_row(9, 2, state->row, "Device type", system->laptop ? "Laptop (TLP enabled)" : "Desktop");
    property_row(11, 3, state->row, "Default locale", locale_name(system->locale));
    property_row(13, 4, state->row, "Package source", system->local_mirror ? "Local F2FS-DATA mirror" : "Network mirror");
    property_row(15, 5, state->row, "Installed system mirrors", system->china_mirrors ? "China mirror list" : "Keep current mirror list");
}

void handle_system(UiState *state, int key)
{
    SystemPlan *system = &state->plan->system;
    if (key == 27) { state->screen = SCREEN_HOME; state->row = 1; return; }
    if (key == KEY_UP && state->row > 0) --state->row;
    else if (key == KEY_DOWN && state->row < 5) ++state->row;
    else if (enter_pressed(key) && inspect_system_packages(state)) return;
    else if (key == ' ' || key == KEY_LEFT || key == KEY_RIGHT || enter_pressed(key)) {
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

/* 硬件页面坚持显式选择，探测结果不会自动开启驱动或服务。 */
void draw_hardware(UiState *state)
{
    SystemPlan *system = &state->plan->system;
    draw_shell(state, "Hardware support", "Up/Down move   Enter packages   Space toggle   Esc back");
    property_row(6, 0, state->row, "Intel integrated graphics", system->intel_graphics ? "Install" : "Skip");
    property_row(8, 1, state->row, "NVIDIA graphics", system->nvidia_graphics ? "Install nvidia-open-dkms" : "Skip");
    property_row(10, 2, state->row, "Bluetooth", system->bluetooth ? "Install and enable" : "Skip");
    mvaddstr(13, 4, "Hardware choices are explicit so detection can never silently install a wrong driver.");
}

void handle_hardware(UiState *state, int key)
{
    static const PackageGroup groups[] = {
        PKG_INTEL_GRAPHICS, PKG_NVIDIA_GRAPHICS, PKG_BLUETOOTH
    };
    bool *values[] = {&state->plan->system.intel_graphics, &state->plan->system.nvidia_graphics,
                     &state->plan->system.bluetooth};
    if (key == 27) { state->screen = SCREEN_HOME; state->row = 2; return; }
    if (key == KEY_UP && state->row > 0) --state->row;
    else if (key == KEY_DOWN && state->row < 2) ++state->row;
    else if (enter_pressed(key)) {
        static const char *const titles[] = {
            "Pacman packages - Intel graphics", "Pacman packages - NVIDIA graphics",
            "Pacman packages - Bluetooth"
        };
        show_packages(state, titles[state->row], &groups[state->row], 1);
    } else if (key == ' ') {
        *values[state->row] = !*values[state->row];
        state->dirty = true;
    }
}

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
    for (int index = 0; index < 9; ++index) property_row(4 + index * 2, index, state->row, names[index], values[index]);
}

void handle_software(UiState *state, int key)
{
    SystemPlan *s = &state->plan->system;
    bool *toggles[] = {&s->desktop_recommended, &s->chinese_input, &s->firewall, &s->printer,
                      &s->archive_tools, &s->terminal_tools, &s->extra_tools, &s->desktop_apps};
    if (key == 27) { state->screen = SCREEN_HOME; state->row = 3; return; }
    if (key == KEY_UP && state->row > 0) --state->row;
    else if (key == KEY_DOWN && state->row < 8) ++state->row;
    else if (enter_pressed(key)) {
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
            if (s->desktop == DESKTOP_KDE) group = PKG_KDE;
            else if (s->desktop == DESKTOP_GNOME) group = PKG_GNOME;
            else if (s->desktop == DESKTOP_HYPRLAND) group = PKG_HYPRLAND;
            else available = false;
            break;
        case 1:
            if (s->desktop == DESKTOP_KDE) group = PKG_KDE_RECOMMENDED;
            else if (s->desktop == DESKTOP_GNOME) group = PKG_GNOME_RECOMMENDED;
            else available = false;
            break;
        case 2:
            if (s->desktop == DESKTOP_GNOME) group = PKG_IBUS;
            else if (s->desktop == DESKTOP_KDE || s->desktop == DESKTOP_HYPRLAND)
                group = PKG_FCITX;
            else available = false;
            break;
        case 3: group = PKG_FIREWALL; break;
        case 4: group = PKG_PRINTER; break;
        case 5: group = PKG_ARCHIVE_TOOLS; break;
        case 6: group = PKG_TERMINAL_TOOLS; break;
        case 7: group = PKG_EXTRA_TOOLS; break;
        default: group = PKG_DESKTOP_APPS; break;
        }
        show_packages(state, titles[state->row], available ? &group : NULL,
                      available ? 1 : 0);
    } else if (key == ' ') {
        if (state->row == 0) s->desktop = (Desktop)(((int)s->desktop + 1) % 4);
        else *toggles[state->row - 1] = !*toggles[state->row - 1];
        state->dirty = true;
    }
}

/* 身份与启动页面包含文本字段、EFI 写入开关和 Secure Boot 开关。 */
void draw_identity(UiState *state)
{
    SystemPlan *s = &state->plan->system;
    const char *keys = state->row == 7
        ? "Up/Down move   Enter packages   Space toggle   Esc back"
        : "Up/Down move   Enter/Space edit or toggle   Esc back";
    draw_shell(state, "Identity and boot", keys);
    property_row(5, 0, state->row, "Hostname", s->hostname);
    property_row(7, 1, state->row, "Username", s->username);
    property_row(9, 2, state->row, "Timezone", s->timezone);
    property_row(11, 3, state->row, "Root password", "Prompt during installation");
    property_row(13, 4, state->row, "User password", "Prompt during installation");
    property_row(15, 5, state->row, "Bootloader", "systemd-boot");
    property_row(17, 6, state->row, "Create EFI NVRAM entry", s->create_efi_entry ? "Yes" : "No");
    property_row(19, 7, state->row, "Shim/MOK (kernel only)", s->secure_boot ? "Enabled" : "Disabled");
}

void handle_identity(UiState *state, int key)
{
    SystemPlan *s = &state->plan->system;
    if (key == 27) { state->screen = SCREEN_HOME; state->row = 4; return; }
    if (key == KEY_UP && state->row > 0) --state->row;
    else if (key == KEY_DOWN && state->row < 7) ++state->row;
    else if (enter_pressed(key) && state->row == 7) {
        PackageGroup group = PKG_SECURE_BOOT_LIVE;
        show_packages(state, "Pacman packages - Secure Boot", &group, 1);
    }
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

/*
 * 审阅页面将存储操作、系统摘要和验证结果整理成一个带颜色的滚动列表。
 * 只有验证通过且磁盘身份仍匹配时，generate() 才会真正生成脚本。
 */
void draw_review(UiState *state)
{
    ValidationReport report;
    StoragePlan *storage = &state->plan->storage;
    char lines[AI_MAX_PLAN_DISKS * (AI_MAX_PARTITIONS + 2) + AI_MAX_ISSUES + 20][256];
    int colors[AI_MAX_PLAN_DISKS * (AI_MAX_PARTITIONS + 2) + AI_MAX_ISSUES + 20] = {0};
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
    for (size_t disk_index = 0; disk_index < storage->disk_count; ++disk_index) {
        const DiskPlan *disk = &storage->disks[disk_index];
        (void)snprintf(lines[count], sizeof(lines[count]), "%s %.120s (%.100s) — %s",
                       disk->mode == STORAGE_EXISTING ? "DISK " : "ERASE",
                       disk->path, disk->model, storage_mode_name(disk->mode));
        colors[count++] = disk->mode == STORAGE_EXISTING ? COLOR_TITLE : COLOR_ERROR;
        for (size_t index = 0; index < disk->partition_count; ++index) {
            const PartitionPlan *part = &disk->partitions[index];
            if ((part->usage == PART_UNUSED && part->action != ACTION_FORMAT && !part->planned) ||
                count >= sizeof(lines) / sizeof(lines[0])) continue;
            (void)snprintf(lines[count], sizeof(lines[count]), "  %-7s %-20.120s -> %-8.8s %.40s",
                           part->action == ACTION_FORMAT ? (part->planned ? "CREATE" : "FORMAT") : "KEEP",
                           part->device,
                           part->action == ACTION_FORMAT ? filesystem_name(part->target_fs) : part->current_fs,
                           partition_mountpoint(part->usage));
            colors[count++] = part->action == ACTION_FORMAT ? COLOR_WARNING : 0;
        }
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
                  "ERROR One or more disk identities no longer match the saved plan.");
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

void handle_review(UiState *state, int key)
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

/* 预览页面逐行读取刚生成的脚本，不把整个脚本长期保存在 UI 状态中。 */
void draw_preview(UiState *state)
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

void handle_preview(UiState *state, int key)
{
    if (key == 27) { state->screen = SCREEN_REVIEW; return; }
    if ((key == KEY_UP || key == KEY_PPAGE) && state->preview_offset > 0)
        state->preview_offset -= key == KEY_PPAGE ? 10 : 1;
    else if (key == KEY_DOWN) ++state->preview_offset;
    else if (key == KEY_NPAGE) state->preview_offset += 10;
    else if (key == 'g' || key == 'G') (void)generate(state);
    if (state->preview_offset < 0) state->preview_offset = 0;
}
