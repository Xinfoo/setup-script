#define _POSIX_C_SOURCE 200809L

#include "private.h"

#include "detect.h"
#include "util.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

bool disk_identity_matches(const InstallPlan *plan, const HardwareInventory *inventory)
{
    for (size_t disk_index = 0; disk_index < plan->storage.disk_count; ++disk_index) {
        const DiskPlan *planned_disk = &plan->storage.disks[disk_index];
        const DiskInfo *disk = inventory_find_disk(inventory, planned_disk->path);
        if (disk == NULL || disk->read_only || disk->size_bytes != planned_disk->size_bytes) return false;
        if (planned_disk->serial[0] != '\0' && strcmp(disk->serial, planned_disk->serial) != 0)
            return false;
        if (planned_disk->model[0] != '\0' && strcmp(disk->model, planned_disk->model) != 0)
            return false;
        if (planned_disk->mode != STORAGE_EXISTING) continue;
        if (strcasecmp(disk->partition_table, planned_disk->partition_table) != 0) return false;
        for (size_t index = 0; index < planned_disk->partition_count; ++index) {
            const PartitionPlan *planned = &planned_disk->partitions[index];
            const PartitionInfo *current = NULL;
            if (planned->usage == PART_UNUSED && planned->action != ACTION_FORMAT) continue;
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

static int disk_dialog(const HardwareInventory *inventory, InstallPlan *plan)
{
    const char *options[AI_MAX_DISKS];
    char labels[AI_MAX_DISKS][256];
    int selected = 0;
    for (size_t index = 0; index < inventory->disk_count; ++index) {
        char size[32];
        char flags[24];
        const DiskInfo *disk = &inventory->disks[index];
        format_size(disk->size_bytes, size, sizeof(size));
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
        (void)snprintf(labels[index], sizeof(labels[index]),
                       "[%-13.13s] %-18.18s %9.9s SN:%-16.16s %.80s",
                       flags, disk->path, size,
                       disk->serial[0] != '\0' ? disk->serial : "-",
                       disk->model[0] != '\0' ? disk->model : "Unknown model");
        options[index] = labels[index];
    }
    return choose_dialog("Add disk to installation plan", options,
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
    state->target_identity_matches = disk_identity_matches(state->plan, state->inventory);
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
    state->target_identity_matches = disk_identity_matches(state->plan, state->inventory);
    set_status(state, "Existing partitions loaded. KEEP never formats a filesystem.");
}

static bool refresh_disks(UiState *state)
{
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
    state->target_identity_matches = disk_identity_matches(state->plan, state->inventory);
    if (!state->target_identity_matches) {
        set_status(state, "Warning: target path now refers to a missing or different disk.");
    } else {
        set_status(state, "Block device inventory refreshed.");
    }
    return true;
}

static int run_cfdisk_process(const char *device, char *error, size_t error_size)
{
    pid_t child;
    int wait_status = 0;

    (void)def_prog_mode();
    (void)endwin();
    child = fork();
    if (child == 0) {
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
    state->target_identity_matches = disk_identity_matches(state->plan, state->inventory);
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

static void remove_active_disk(UiState *state)
{
    StoragePlan *storage = &state->plan->storage;
    if (state->active_disk >= storage->disk_count) return;
    if (!confirm_dialog("Remove disk from plan",
                        "Remove the active disk and all of its partition assignments?")) return;
    if (state->active_disk + 1 < storage->disk_count) {
        memmove(&storage->disks[state->active_disk],
                &storage->disks[state->active_disk + 1],
                (storage->disk_count - state->active_disk - 1) * sizeof(storage->disks[0]));
    }
    --storage->disk_count;
    memset(&storage->disks[storage->disk_count], 0, sizeof(storage->disks[0]));
    if (state->active_disk >= storage->disk_count && state->active_disk > 0) --state->active_disk;
    state->row = -1;
    state->dirty = true;
    state->target_identity_matches = disk_identity_matches(state->plan, state->inventory);
    set_status(state, "Disk removed from the installation plan; no device was modified.");
}

void draw_storage(UiState *state)
{
    StoragePlan *storage = &state->plan->storage;
    const char *keys;
    int available = LINES - 10;
    int selected_line = 0;
    int visual_line = 0;
    int offset;

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
            keys = "Up/Down move   D add disk   U/Space mount   F/Enter format   O F2FS   R refresh   Esc back";
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
    attron(A_BOLD);
    mvprintw(5, 2, "%-18s %8s %-8s %-8s %-7s %-7s %-9s",
             "Device", "Size", "Current", "Action", "Target", "Purpose", "Mount");
    if (COLS >= 96) addstr(" Profile");
    attroff(A_BOLD);

    for (size_t disk_index = 0; disk_index < state->active_disk; ++disk_index)
        selected_line += 1 + (int)storage->disks[disk_index].partition_count;
    selected_line += 1 + state->row;
    offset = selected_line >= available ? selected_line - available + 1 : 0;

    for (size_t disk_index = 0; disk_index < storage->disk_count; ++disk_index) {
        const DiskPlan *disk = &storage->disks[disk_index];
        char disk_size[32];
        bool selected = disk_index == state->active_disk && state->row < 0;
        format_size(disk->size_bytes, disk_size, sizeof(disk_size));
        if (visual_line >= offset && visual_line < offset + available) {
            int y = 6 + visual_line - offset;
            attron(A_BOLD | COLOR_PAIR(selected ? COLOR_SELECTED :
                                        (disk->mode == STORAGE_EXISTING ? COLOR_TITLE : COLOR_ERROR)));
            mvprintw(y, 2, "%c DISK %-18.18s %8.8s %-28.28s  %s",
                     disk_index == state->active_disk ? '>' : ' ', disk->path, disk_size,
                     disk->model[0] != '\0' ? disk->model : "Unknown model",
                     storage_mode_name(disk->mode));
            attroff(A_BOLD | COLOR_PAIR(selected ? COLOR_SELECTED :
                                         (disk->mode == STORAGE_EXISTING ? COLOR_TITLE : COLOR_ERROR)));
        }
        ++visual_line;
        for (size_t index = 0; index < disk->partition_count; ++index, ++visual_line) {
            const PartitionPlan *part = &disk->partitions[index];
            char size[32];
            char operation[16];
            bool partition_selected = disk_index == state->active_disk &&
                                      (int)index == state->row;
            if (visual_line < offset || visual_line >= offset + available) continue;
            format_size(part->size_bytes, size, sizeof(size));
            if (part->planned) copy_text(operation, sizeof(operation), "CREATE");
            else if (part->usage == PART_UNUSED && part->action == ACTION_KEEP)
                copy_text(operation, sizeof(operation), "IGNORE");
            else copy_text(operation, sizeof(operation),
                           part->action == ACTION_FORMAT ? "FORMAT" : "KEEP");
            if (partition_selected) attron(COLOR_PAIR(COLOR_SELECTED));
            mvprintw(6 + visual_line - offset, 2,
                     "  %-16.16s %8.8s %-8.8s %-8.8s %-7.7s %-7.7s %-9.9s",
                     part->device, size, part->current_fs[0] != '\0' ? part->current_fs : "-",
                     operation, part->action == ACTION_FORMAT ? filesystem_name(part->target_fs) :
                     (part->current_fs[0] != '\0' ? part->current_fs : "-"),
                     usage_name(part->usage), partition_mountpoint(part->usage));
            if (COLS >= 96) {
                Filesystem effective = part->action == ACTION_FORMAT ? part->target_fs :
                                       filesystem_from_name(part->current_fs);
                printw(" %s", effective == FS_F2FS ? f2fs_mode_name(part->f2fs_mode) : "-");
            }
            if (partition_selected) attroff(COLOR_PAIR(COLOR_SELECTED));
        }
    }
}

static bool regular_filesystem(Filesystem filesystem)
{
    return filesystem == FS_EXT4 || filesystem == FS_XFS || filesystem == FS_F2FS;
}

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
    Filesystem current;
    int choice;

    if (partition->planned && disk->mode != STORAGE_AUTO_DATA) {
        set_status(state, "Guided-layout purposes are fixed; choose another layout to change them.");
        return;
    }
    choice = choose_dialog("Assign partition purpose", options,
                           sizeof(options) / sizeof(options[0]), (int)partition->usage);
    if (choice < 0) return;

    partition->usage = (PartitionUsage)choice;
    current = filesystem_from_name(partition->current_fs);
    if (partition->usage == PART_UNUSED) {
        if (partition->planned && disk->mode == STORAGE_AUTO_DATA) {
            partition->action = ACTION_FORMAT;
            if (partition->target_fs == FS_NONE) partition->target_fs = FS_EXT4;
        } else {
            partition->action = ACTION_KEEP;
            partition->target_fs = FS_NONE;
        }
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
        !(partition->planned &&
          (partition->usage == PART_BOOT || partition->usage == PART_SWAP)) &&
        !edit_partition_format(state, partition)) {
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
    Filesystem current = filesystem_from_name(partition->current_fs);

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
        if (partition->usage == PART_UNUSED) {
            options[count] = "FORMAT as FAT32 (no mount point)";
            actions[count] = ACTION_FORMAT;
            filesystems[count++] = FS_VFAT;
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
        edit_f2fs_mode(state, &disk->partitions[state->row]);
    }
}
