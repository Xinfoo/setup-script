#define _POSIX_C_SOURCE 200809L

#include "private.h"

#include "detector.h"
#include "text.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

/* 磁盘方案操作：添加磁盘、选择引导式布局、恢复现有分区以及刷新探测结果。 */
static int disk_dialog(const HardwareInventory *inventory, InstallPlan *plan)
{
    static const UiTableColumn columns[] = {
        {"Status", 15, 6, UI_ALIGN_LEFT},
        {"Device", 18, 8, UI_ALIGN_LEFT},
        {"Size", 9, 4, UI_ALIGN_RIGHT},
        {"Serial", 16, 6, UI_ALIGN_LEFT},
        {"Model", 30, 8, UI_ALIGN_LEFT}
    };
    const char *cells[AI_MAX_DISKS * (sizeof(columns) / sizeof(columns[0]))];
    char statuses[AI_MAX_DISKS][32];
    char sizes[AI_MAX_DISKS][32];
    int selected = 0;

    for (size_t index = 0; index < inventory->disk_count; ++index) {
        char flags[24];
        const DiskInfo *disk = &inventory->disks[index];
        size_t cell = index * (sizeof(columns) / sizeof(columns[0]));

        format_size(disk->size_bytes, sizes[index], sizeof(sizes[index]));
        if (plan_find_disk(plan, disk->path) != NULL) {
            copy_text(flags, sizeof(flags), "ADDED");
        } else if (!disk->read_only && !disk->removable && !disk->in_use) {
            copy_text(flags, sizeof(flags), "OK");
        } else {
            (void)snprintf(flags, sizeof(flags), "%s%s%s",
                           disk->read_only ? "RO " : "",
                           disk->removable ? "USB " : "",
                           disk->in_use ? "IN-USE" : "");
        }
        (void)snprintf(statuses[index], sizeof(statuses[index]), "[%s]", flags);
        cells[cell] = statuses[index];
        cells[cell + 1] = disk->path;
        cells[cell + 2] = sizes[index];
        cells[cell + 3] = disk->serial[0] != '\0' ? disk->serial : "-";
        cells[cell + 4] = disk->model[0] != '\0' ? disk->model : "Unknown model";
    }
    return choose_table_dialog("Add disk to installation plan", columns,
                               sizeof(columns) / sizeof(columns[0]), cells,
                               inventory->disk_count, selected);
}

static void add_disk(UiState *state)
{
    StoragePlan *storage = &state->plan->storage;
    int choice = disk_dialog(state->inventory, state->plan);
    if (choice < 0) return;
    const DiskInfo *disk = &state->inventory->disks[choice];
    DiskPlan *existing = plan_find_disk(state->plan, disk->path);
    if (existing != NULL) {
        set_status(state, "That disk is already in the plan; use Up/Down to reach it.");
        return;
    }
    if (disk->read_only) {
        set_status(state, "The selected disk is read-only and cannot be an installation target.");
        return;
    }
    if (disk->removable && !confirm_dialog("Removable disk",
        "This disk is removable or connected by USB. Select it anyway?")) return;
    if (!plan_add_disk(state->plan, disk)) {
        set_status(state, "The installation plan already contains the maximum number of disks.");
        return;
    }
    state->active_disk = storage->disk_count - 1;
    state->target_identity_matches = plan_storage_matches_inventory(state->plan, state->inventory);
    state->row = -1;
    state->dirty = true;
    set_status(state, "Disk added with its existing partitions untouched.");
}

static void choose_layout(UiState *state)
{
    static const char *const options[] = {
        "Root + recommended swap",
        "100 GiB root + home + recommended swap",
        "Root only, no swap",
        "Single whole-disk data partition (no mount point yet)"
    };
    StoragePlan *storage = &state->plan->storage;
    DiskPlan *planned_disk;
    const DiskInfo *disk;
    int choice;
    if (state->active_disk >= storage->disk_count) {
        set_status(state, "Add an installation disk first.");
        return;
    }
    planned_disk = &storage->disks[state->active_disk];
    disk = inventory_find_disk(state->inventory, planned_disk->path);
    if (disk == NULL) {
        set_status(state, "The active disk is no longer present.");
        return;
    }
    choice = choose_dialog("Guided whole-disk layout (active disk only)", options, 4,
                           planned_disk->mode == STORAGE_AUTO_DATA ? 3 : 0);
    if (choice < 0) return;
    if (!confirm_dialog("Replace partition table",
        "The generated script will erase every partition on the active disk. Continue planning?")) return;
    disk_plan_use_automatic(planned_disk, disk, choice == 0 ? STORAGE_AUTO_ROOT_SWAP :
                            choice == 1 ? STORAGE_AUTO_HOME_SWAP :
                            choice == 2 ? STORAGE_AUTO_ROOT_ONLY : STORAGE_AUTO_DATA);
    state->row = -1;
    state->dirty = true;
    set_status(state, "Guided layout applied to the plan; no disk was modified.");
}

static void use_existing(UiState *state)
{
    StoragePlan *storage = &state->plan->storage;
    DiskPlan *planned_disk;
    const DiskInfo *disk;
    if (state->active_disk >= storage->disk_count) {
        set_status(state, "Add an installation disk first.");
        return;
    }
    planned_disk = &storage->disks[state->active_disk];
    disk = inventory_find_disk(state->inventory, planned_disk->path);
    if (disk == NULL) {
        set_status(state, "The active disk is no longer present.");
        return;
    }
    if (!confirm_dialog("Use existing partitions", "Reset assignments on the active disk from detected partitions?")) return;
    disk_plan_use_existing(planned_disk, disk);
    state->row = -1;
    state->dirty = true;
    state->target_identity_matches = plan_storage_matches_inventory(state->plan, state->inventory);
    set_status(state, "Existing partitions loaded. KEEP never formats a filesystem.");
}

static bool refresh_disks(UiState *state)
{
    /* HardwareInventory 较大，临时放在堆上，成功后再整体替换当前探测快照。 */
    HardwareInventory *inventory = calloc(1, sizeof(*inventory));
    char error[512] = {0};
    if (inventory == NULL) {
        set_status(state, "Refresh failed: out of memory.");
        return false;
    }
    if (!detect_hardware(inventory, error, sizeof(error))) {
        (void)snprintf(state->status, sizeof(state->status), "Refresh failed: %.190s", error);
        free(inventory);
        return false;
    }
    *state->inventory = *inventory;
    free(inventory);
    state->target_identity_matches = plan_storage_matches_inventory(state->plan, state->inventory);
    if (!state->target_identity_matches) {
        set_status(state, "Warning: target path now refers to a missing or different disk.");
    } else {
        set_status(state, "Block device inventory refreshed.");
    }
    return true;
}

/*
 * 临时退出 ncurses，将终端完整交给 cfdisk；子进程结束后恢复程序模式并
 * 强制重绘。上层随后重新探测磁盘，旧的挂载点分派不会跨分区表编辑保留。
 */
static int run_cfdisk_process(const char *device, char *error, size_t error_size)
{
    pid_t child;
    int wait_status = 0;

    /* 保存 ncurses 的终端模式后 endwin，避免 cfdisk 继承半初始化的屏幕状态。 */
    (void)def_prog_mode();
    (void)endwin();
    child = fork();
    if (child == 0) {
        /* 使用固定绝对路径和参数向量，不经过 Shell 展开设备名。 */
        execl("/usr/bin/cfdisk", "cfdisk", device, (char *)NULL);
        _exit(127);
    }
    if (child < 0) {
        (void)snprintf(error, error_size, "cannot start cfdisk: %s", strerror(errno));
    } else {
        while (waitpid(child, &wait_status, 0) < 0) {
            if (errno == EINTR) continue;
            (void)snprintf(error, error_size, "cannot wait for cfdisk: %s", strerror(errno));
            child = -1;
            break;
        }
    }
    /* 无论子进程如何退出，都先恢复终端并标记整屏失效，再交回事件循环。 */
    (void)reset_prog_mode();
    (void)curs_set(0);
    clearok(stdscr, TRUE);
    touchwin(stdscr);

    if (child < 0) return -1;
    if (WIFEXITED(wait_status)) return WEXITSTATUS(wait_status);
    if (WIFSIGNALED(wait_status)) return 128 + WTERMSIG(wait_status);
    return 1;
}

static void launch_cfdisk(UiState *state)
{
    StoragePlan *storage = &state->plan->storage;
    DiskPlan *planned_disk;
    const DiskInfo *detected_disk;
    char title[96];
    char warning[256];
    char error[256] = {0};
    int status;

    if (state->active_disk >= storage->disk_count || state->row >= 0) return;
    planned_disk = &storage->disks[state->active_disk];
    if (geteuid() != 0) {
        set_status(state, "cfdisk requires the builder to be running as root.");
        return;
    }
    if (planned_disk->mode != STORAGE_EXISTING) {
        set_status(state, "cfdisk is unavailable while this disk uses an automatic layout.");
        return;
    }
    if (access("/usr/bin/cfdisk", X_OK) != 0) {
        set_status(state, "Cannot start cfdisk: /usr/bin/cfdisk is not available.");
        return;
    }
    (void)snprintf(title, sizeof(title), "Run cfdisk on %.64s", planned_disk->path);
    (void)snprintf(warning, sizeof(warning),
                   "cfdisk edits %.100s directly and may destroy data. After it exits, this disk's partition assignments will be reloaded. Continue?",
                   planned_disk->path);
    if (!confirm_dialog(title, warning)) return;

    status = run_cfdisk_process(planned_disk->path, error, sizeof(error));
    /*
     * cfdisk 可能改变分区数量和所有身份字段，因此不尝试合并旧分派；
     * 重新探测后整盘装载为“现有分区、全部忽略”的干净状态。
     */
    if (!refresh_disks(state)) return;
    detected_disk = inventory_find_disk(state->inventory, planned_disk->path);
    if (detected_disk == NULL) {
        set_status(state, "cfdisk exited, but the edited disk was not found during refresh.");
        state->target_identity_matches = false;
        return;
    }
    copy_text(planned_disk->partition_table, sizeof(planned_disk->partition_table),
              detected_disk->partition_table);
    planned_disk->in_use = detected_disk->in_use;
    disk_plan_use_existing(planned_disk, detected_disk);
    state->row = -1;
    state->dirty = true;
    state->target_identity_matches = plan_storage_matches_inventory(state->plan, state->inventory);
    if (status < 0) {
        (void)snprintf(state->status, sizeof(state->status),
                       "%.150s; disk list refreshed.", error);
    } else if (status == 0) {
        set_status(state, "cfdisk exited; the disk and partition list was refreshed.");
    } else {
        (void)snprintf(state->status, sizeof(state->status),
                       "cfdisk exited with status %d; disk list refreshed.", status);
    }
}

/* 从方案移除磁盘只修改内存模型，不会对真实磁盘执行任何操作。 */
static void remove_active_disk(UiState *state)
{
    StoragePlan *storage = &state->plan->storage;
    if (state->active_disk >= storage->disk_count) return;
    if (!confirm_dialog("Remove disk from plan",
                        "Remove the active disk and all of its partition assignments?")) return;
    if (!plan_remove_disk_at(state->plan, state->active_disk)) return;
    if (state->active_disk >= storage->disk_count && state->active_disk > 0) --state->active_disk;
    state->row = -1;
    state->dirty = true;
    state->target_identity_matches = plan_storage_matches_inventory(state->plan, state->inventory);
    set_status(state, "Disk removed from the installation plan; no device was modified.");
}

static const UiTableColumn storage_columns[] = {
    {"Device", 11, 8, UI_ALIGN_LEFT},
    {"Size", 7, 7, UI_ALIGN_RIGHT},
    {"Current", 7, 7, UI_ALIGN_LEFT},
    {"Action", 6, 6, UI_ALIGN_LEFT},
    {"Target", 6, 6, UI_ALIGN_LEFT},
    {"Purpose", 7, 7, UI_ALIGN_LEFT},
    {"Mount", 7, 5, UI_ALIGN_LEFT},
    {"Options", 12, 7, UI_ALIGN_LEFT}
};

/* 磁盘组表头使用独立列模型，但与分区表共享同一布局计算和裁切实现。 */
static const UiTableColumn disk_group_columns[] = {
    {"Disk", 26, 18, UI_ALIGN_LEFT},
    {"Size", 9, 7, UI_ALIGN_RIGHT},
    {"Model", 28, 12, UI_ALIGN_LEFT},
    {"Layout", 28, 12, UI_ALIGN_LEFT}
};

static void draw_disk_group(int y, const UiTableLayout *layout,
                            const DiskPlan *disk, bool active, bool selected)
{
    char size[32];
    char device[AI_PATH_LEN + 8];
    const char *values[sizeof(disk_group_columns) / sizeof(disk_group_columns[0])];
    int color = selected ? COLOR_SELECTED :
                (disk->mode == STORAGE_EXISTING ? COLOR_TITLE : COLOR_ERROR);

    format_size(disk->size_bytes, size, sizeof(size));
    (void)snprintf(device, sizeof(device), "%c DISK %s", active ? '>' : ' ', disk->path);
    values[0] = device;
    values[1] = size;
    values[2] = disk->model[0] != '\0' ? disk->model : "Unknown model";
    values[3] = storage_mode_name(disk->mode);
    attron(A_BOLD | COLOR_PAIR(color));
    draw_table_row(stdscr, y, layout, disk_group_columns, values);
    attroff(A_BOLD | COLOR_PAIR(color));
}

static void draw_partition_row(int y, const UiTableLayout *layout,
                               const PartitionPlan *partition, bool selected)
{
    char size[32];
    const char *operation;
    const char *current = partition->current_fs[0] != '\0' ? partition->current_fs : "-";
    const char *target = partition->action == ACTION_FORMAT ?
                         filesystem_name(partition->target_fs) : current;
    const char *options = partition_supports_mount_profile(
                              partition, MOUNT_PROFILE_DEFAULT) ?
                          mount_profile_name(partition->mount_profile) : "-";
    const char *values[sizeof(storage_columns) / sizeof(storage_columns[0])];

    format_size(partition->size_bytes, size, sizeof(size));
    /* 显示动作同时考虑来源与用途：未分配的 KEEP 分区在执行阶段会被忽略。 */
    if (partition->planned) operation = "CREATE";
    else if (partition->usage == PART_UNUSED && partition->action == ACTION_KEEP)
        operation = "IGNORE";
    else operation = partition->action == ACTION_FORMAT ? "FORMAT" : "KEEP";
    values[0] = partition->device;
    values[1] = size;
    values[2] = current;
    values[3] = operation;
    values[4] = target;
    values[5] = usage_name(partition->usage);
    values[6] = partition_mountpoint(partition->usage);
    values[7] = options;

    if (selected) attron(COLOR_PAIR(COLOR_SELECTED));
    draw_table_row(stdscr, y, layout, storage_columns, values);
    if (selected) attroff(COLOR_PAIR(COLOR_SELECTED));
}

/*
 * Storage 表把每块磁盘的表头和分区组成连续的可滚动行。
 * state->row == -1 表示选中磁盘表头，非负值表示当前磁盘内的分区下标。
 */
void draw_storage(UiState *state)
{
    StoragePlan *storage = &state->plan->storage;
    const char *keys;
    int available = LINES - 10;
    int selected_line = 0;
    int visual_line = 0;
    int offset;
    UiTableLayout table;
    UiTableLayout disk_table;

    if (storage->disk_count == 0) {
        keys = "Up/Down move   D add disk   R refresh   Esc back";
    } else {
        if (state->active_disk >= storage->disk_count) state->active_disk = 0;
        if (state->row < -1) state->row = -1;
        if (state->row >= 0 &&
            (size_t)state->row >= storage->disks[state->active_disk].partition_count) {
            size_t count = storage->disks[state->active_disk].partition_count;
            state->row = count == 0 ? -1 : (int)count - 1;
        }
        if (state->row < 0) {
            keys = storage->disks[state->active_disk].mode == STORAGE_EXISTING ?
                   "Up/Down move   D add disk   C cfdisk   X remove   A layout   E existing   R refresh   Esc back" :
                   "Up/Down move   D add disk   X remove   A layout   E existing   R refresh   Esc back";
        } else {
            keys = "Up/Down move   D add disk   U/Space mount   F/Enter format   O options   R refresh   Esc back";
        }
    }
    draw_shell(state, "Storage plan - editing changes only the generated plan", keys);
    if (storage->disk_count == 0) {
        attron(COLOR_PAIR(COLOR_WARNING));
        mvaddstr(5, 4, "No installation disk added.");
        attroff(COLOR_PAIR(COLOR_WARNING));
        mvaddstr(7, 4, "Press D to add a disk. This program only edits the generated plan.");
        return;
    }
    mvprintw(4, 2, "%zu disk(s); Up/Down moves across disk groups. FORMAT needs no mount point.",
             storage->disk_count);
    calculate_table_layout(&table, 2, COLS - 4, storage_columns,
                           sizeof(storage_columns) / sizeof(storage_columns[0]), 4);
    calculate_table_layout(&disk_table, 2, COLS - 4, disk_group_columns,
                           sizeof(disk_group_columns) / sizeof(disk_group_columns[0]), 3);
    attron(A_BOLD);
    draw_table_header(stdscr, 5, &table, storage_columns);
    attroff(A_BOLD);

    /* 选中项先换算为包含组间空行的全局逻辑行，再据此移动视口。 */
    for (size_t disk_index = 0; disk_index < state->active_disk; ++disk_index)
        selected_line += 2 + (int)storage->disks[disk_index].partition_count;
    /* 逻辑坐标中表头占一行，所以 row=-1 恰好映射到该组的第一行。 */
    selected_line += 1 + state->row;
    offset = selected_line >= available ? selected_line - available + 1 : 0;

    for (size_t disk_index = 0; disk_index < storage->disk_count; ++disk_index) {
        const DiskPlan *disk = &storage->disks[disk_index];
        bool selected = disk_index == state->active_disk && state->row < 0;

        /* 每个后续磁盘组前保留一行，并让它参与统一的滚动坐标计算。 */
        if (disk_index > 0) ++visual_line;
        if (visual_line >= offset && visual_line < offset + available) {
            int y = 7 + visual_line - offset;
            draw_disk_group(y, &disk_table, disk,
                            disk_index == state->active_disk, selected);
        }
        ++visual_line;
        for (size_t index = 0; index < disk->partition_count; ++index, ++visual_line) {
            const PartitionPlan *part = &disk->partitions[index];
            bool partition_selected = disk_index == state->active_disk &&
                                      (int)index == state->row;
            if (visual_line < offset || visual_line >= offset + available) continue;
            draw_partition_row(7 + visual_line - offset, &table, part,
                               partition_selected);
        }
    }
}

/* 分区编辑分为用途、格式化动作和挂载选项三层。 */
static bool edit_partition_format(UiState *state, PartitionPlan *partition);

static void edit_partition_usage(UiState *state, DiskPlan *disk, PartitionPlan *partition)
{
    static const char *const options[] = {
        "No mount point (use F to choose IGNORE or FORMAT)",
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
    int choice;

    if (partition->planned && disk->mode != STORAGE_AUTO_DATA) {
        set_status(state, "Guided-layout purposes are fixed; choose another layout to change them.");
        return;
    }
    choice = choose_dialog("Assign partition purpose", options,
                           sizeof(options) / sizeof(options[0]), (int)partition->usage);
    if (choice < 0) return;

    if (!partition_plan_assign_usage(partition, disk->mode, (PartitionUsage)choice)) return;

    if (partition->usage != PART_UNUSED && partition->usage != old_usage &&
        !(partition->planned &&
          (partition->usage == PART_BOOT || partition->usage == PART_SWAP)) &&
        !edit_partition_format(state, partition)) {
        /* 第二个对话框取消时恢复整份结构，避免只改了用途却没确认格式化动作。 */
        *partition = original;
        set_status(state, "Purpose change canceled; no partition action was changed.");
        return;
    }

    if (partition->usage != old_usage || partition->action != old_action ||
        partition->target_fs != old_target) {
        state->dirty = true;
        if (partition->usage == PART_UNUSED) {
            if (partition->action == ACTION_FORMAT) {
                (void)snprintf(state->status, sizeof(state->status),
                               "%.180s will be formatted without a mount point.",
                               partition->device);
            } else {
                (void)snprintf(state->status, sizeof(state->status),
                               "%.180s is ignored and will not be mounted or formatted.",
                               partition->device);
            }
        }
    }
}

static bool edit_partition_format(UiState *state, PartitionPlan *partition)
{
    const char *options[6];
    PartitionAction actions[6];
    Filesystem filesystems[6];
    char keep_label[96];
    size_t count = 0;
    int current_choice = 0;
    int choice;
    bool changed;
    Filesystem current = filesystem_from_name(partition->current_fs);

    if (partition->planned &&
        (partition->usage == PART_BOOT || partition->usage == PART_SWAP)) {
        set_status(state, partition->usage == PART_BOOT ?
                   "Guided EFI partitions are always created as FAT32." :
                   "Guided swap partitions are always created with mkswap.");
        return false;
    }

    /*
     * KEEP 仅在现有格式能承担当前用途时出现；root、/var、/usr 为全新系统的
     * 关键目录，即使格式可识别也只提供 FORMAT。
     */
    if (partition_plan_action_allowed(partition, ACTION_KEEP, FS_NONE)) {
        (void)snprintf(keep_label, sizeof(keep_label),
                       "KEEP current %s filesystem (no formatting)",
                       filesystem_name(current));
        options[count] = keep_label;
        actions[count] = ACTION_KEEP;
        filesystems[count++] = FS_NONE;
    }

    if (partition_plan_action_allowed(partition, ACTION_FORMAT, FS_VFAT) &&
        partition->usage == PART_BOOT) {
        options[count] = "FORMAT as FAT32 (erases this partition)";
        actions[count] = ACTION_FORMAT;
        filesystems[count++] = FS_VFAT;
    } else if (partition_plan_action_allowed(partition, ACTION_FORMAT, FS_SWAP) &&
               partition->usage == PART_SWAP) {
        options[count] = "FORMAT as swap (erases this partition)";
        actions[count] = ACTION_FORMAT;
        filesystems[count++] = FS_SWAP;
    } else {
        static const Filesystem regular[] = {FS_EXT4, FS_XFS, FS_F2FS};
        static const char *const labels[] = {
            "FORMAT as ext4 (erases this partition)",
            "FORMAT as XFS (erases this partition)",
            "FORMAT as F2FS (erases this partition)"
        };
        for (size_t index = 0; index < sizeof(regular) / sizeof(regular[0]); ++index) {
            if (!partition_plan_action_allowed(partition, ACTION_FORMAT, regular[index])) continue;
            options[count] = labels[index];
            actions[count] = ACTION_FORMAT;
            filesystems[count++] = regular[index];
        }
        if (partition_plan_action_allowed(partition, ACTION_FORMAT, FS_VFAT)) {
            options[count] = "FORMAT as FAT32 (no mount point)";
            actions[count] = ACTION_FORMAT;
            filesystems[count++] = FS_VFAT;
        }
        if (partition_plan_action_allowed(partition, ACTION_FORMAT, FS_SWAP)) {
            options[count] = "FORMAT as swap (do not activate)";
            actions[count] = ACTION_FORMAT;
            filesystems[count++] = FS_SWAP;
        }
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
    changed = partition->action != actions[choice] ||
              partition->target_fs != filesystems[choice];
    if (!partition_plan_set_action(partition, actions[choice], filesystems[choice])) {
        set_status(state, "That filesystem action is not valid for this partition purpose.");
        return false;
    }
    if (changed) state->dirty = true;
    if (partition->action == ACTION_KEEP) {
        (void)snprintf(state->status, sizeof(state->status),
                       "%.180s will be kept without formatting.", partition->device);
    } else {
        (void)snprintf(state->status, sizeof(state->status),
                       "%.180s will be formatted as %s.", partition->device,
                       filesystem_name(partition->target_fs));
    }
    return true;
}

/*
 * O 键统一编辑挂载选项。当前非默认配置只属于新格式化的 F2FS，其他可挂载
 * 文件系统仍进入同一对话框，但只提供 Default，便于以后直接扩充各自选项。
 */
static void edit_mount_options(UiState *state, PartitionPlan *partition)
{
    const char *options[3] = {"Default mount options"};
    MountProfile profiles[3] = {MOUNT_PROFILE_DEFAULT};
    Filesystem effective = partition_effective_filesystem(partition);
    size_t count = 1;
    int current_choice = 0;
    int choice;

    if (!partition_supports_mount_profile(partition, MOUNT_PROFILE_DEFAULT)) {
        set_status(state, "Mount options apply only to partitions with a mount point.");
        return;
    }

    for (MountProfile profile = MOUNT_PROFILE_BALANCED;
         profile <= MOUNT_PROFILE_COMPRESSED;
         profile = (MountProfile)((int)profile + 1)) {
        if (!partition_supports_mount_profile(partition, profile)) continue;
        options[count] = profile == MOUNT_PROFILE_BALANCED
            ? "Balanced: noatime, lazytime, GC tuning"
            : "Compressed: balanced options plus zstd compression";
        profiles[count++] = profile;
    }
    for (size_t index = 0; index < count; ++index) {
        if (profiles[index] == partition->mount_profile) {
            current_choice = (int)index;
            break;
        }
    }
    choice = choose_dialog("Mount options", options, count, current_choice);
    if (choice < 0) return;
    if (partition->mount_profile != profiles[choice]) {
        if (!partition_plan_set_mount_profile(partition, profiles[choice])) return;
        state->dirty = true;
    }
    (void)snprintf(state->status, sizeof(state->status), "%s mount options: %s",
                   filesystem_name(effective), mount_profile_name(partition->mount_profile));
}

/*
 * 上下键在“磁盘表头 + 该盘分区”组成的逻辑列表中连续移动。
 * 表头处理磁盘级命令，分区行处理用途、格式化和挂载配置，二者互不混用。
 */
void handle_storage(UiState *state, int key)
{
    StoragePlan *storage = &state->plan->storage;
    DiskPlan *disk;
    if (key == 27) { state->screen = SCREEN_HOME; state->row = 0; return; }
    if (key == 'd' || key == 'D') { add_disk(state); return; }
    if (key == 'r' || key == 'R') { (void)refresh_disks(state); return; }
    if (state->active_disk >= storage->disk_count) return;
    disk = &storage->disks[state->active_disk];
    if (state->row < -1) state->row = -1;
    if (state->row >= 0 && (size_t)state->row >= disk->partition_count)
        state->row = disk->partition_count == 0 ? -1 : (int)disk->partition_count - 1;
    if (key == KEY_UP) {
        /* 从分区 0 再向上即落到本盘表头；从表头向上进入上一盘的末行。 */
        if (state->row >= 0) {
            --state->row;
        } else if (state->active_disk > 0) {
            --state->active_disk;
            disk = &storage->disks[state->active_disk];
            state->row = disk->partition_count == 0 ? -1 : (int)disk->partition_count - 1;
        }
        return;
    }
    if (key == KEY_DOWN) {
        /* 向下越过末分区时进入下一盘表头，形成跨磁盘的单一连续导航。 */
        if (state->row < (int)disk->partition_count - 1) {
            ++state->row;
        } else if (state->active_disk + 1 < storage->disk_count) {
            ++state->active_disk;
            state->row = -1;
        }
        return;
    }
    if (state->row < 0) {
        if (key == 'c' || key == 'C') { launch_cfdisk(state); return; }
        if (key == 'x' || key == 'X') { remove_active_disk(state); return; }
        if (key == 'a' || key == 'A') { choose_layout(state); return; }
        if (key == 'e' || key == 'E') { use_existing(state); return; }
        if (key == 'u' || key == 'U' || key == ' ' || key == 'f' || key == 'F' ||
            key == '\n' || key == KEY_ENTER || key == 'o' || key == 'O') {
            set_status(state, "Select a partition with Up/Down before editing it.");
        }
        return;
    }
    if (key == 'x' || key == 'X' || key == 'a' || key == 'A' ||
        key == 'c' || key == 'C' || key == 'e' || key == 'E') {
        set_status(state, "Select the disk header with Up/Down for disk-level actions.");
        return;
    }
    if (key == 'u' || key == 'U' || key == ' ') {
        edit_partition_usage(state, disk, &disk->partitions[state->row]);
    } else if (key == 'f' || key == 'F' || key == '\n' || key == KEY_ENTER) {
        edit_partition_format(state, &disk->partitions[state->row]);
    } else if (key == 'o' || key == 'O') {
        edit_mount_options(state, &disk->partitions[state->row]);
    }
}
