#define _POSIX_C_SOURCE 200809L

#include "private.h"

#include "util.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MIB UINT64_C(1048576)

typedef struct {
    const DiskPlan *disk;
    const PartitionPlan *partition;
    size_t disk_index;
} PartitionRef;

typedef enum {
    FIELD_DEVICE,
    FIELD_USAGE,
    FIELD_ACTION,
    FIELD_FILESYSTEM,
    FIELD_F2FS_MODE,
    FIELD_MOUNTPOINT,
    FIELD_FS_UUID,
    FIELD_PART_UUID,
    FIELD_PART_TYPE
} PartitionField;

typedef enum {
    NUMBER_PARTITION,
    NUMBER_START_SECTOR,
    NUMBER_SIZE_BYTES
} PartitionNumberField;

static const unsigned char preamble_template[] = {
#include "generated/generator/preamble.inc"
};

const PartitionPlan *find_partition(const InstallPlan *plan, PartitionUsage usage)
{
    for (size_t disk_index = 0; disk_index < plan->storage.disk_count; ++disk_index) {
        const DiskPlan *disk = &plan->storage.disks[disk_index];
        for (size_t index = 0; index < disk->partition_count; ++index) {
            if (disk->partitions[index].usage == usage) {
                return &disk->partitions[index];
            }
        }
    }
    return NULL;
}

static const DiskPlan *find_partition_disk(const InstallPlan *plan,
                                           const PartitionPlan *partition)
{
    if (partition == NULL) return NULL;
    for (size_t disk_index = 0; disk_index < plan->storage.disk_count; ++disk_index) {
        const DiskPlan *disk = &plan->storage.disks[disk_index];
        if (partition >= disk->partitions &&
            partition < disk->partitions + disk->partition_count) return disk;
    }
    return NULL;
}

static int partition_order(const PartitionRef *left, const PartitionRef *right)
{
    const char *left_mount;
    const char *right_mount;
    const PartitionPlan *left_part = left->partition;
    const PartitionPlan *right_part = right->partition;

    if (left_part->usage == PART_ROOT && right_part->usage != PART_ROOT) {
        return -1;
    }
    if (right_part->usage == PART_ROOT && left_part->usage != PART_ROOT) {
        return 1;
    }
    if (left_part->usage == PART_UNUSED && right_part->usage != PART_UNUSED) {
        return 1;
    }
    if (right_part->usage == PART_UNUSED && left_part->usage != PART_UNUSED) {
        return -1;
    }
    if (left_part->usage == PART_SWAP && right_part->usage != PART_SWAP) {
        return 1;
    }
    if (right_part->usage == PART_SWAP && left_part->usage != PART_SWAP) {
        return -1;
    }
    left_mount = partition_mountpoint(left_part->usage);
    right_mount = partition_mountpoint(right_part->usage);
    return strcmp(left_mount, right_mount);
}

static size_t collect_actionable_partitions(const InstallPlan *plan,
                                            PartitionRef *partitions)
{
    size_t count = 0;

    for (size_t disk_index = 0; disk_index < plan->storage.disk_count; ++disk_index) {
        const DiskPlan *disk = &plan->storage.disks[disk_index];
        for (size_t index = 0; index < disk->partition_count; ++index) {
            const PartitionPlan *partition = &disk->partitions[index];
            PartitionRef entry;
            if (partition->usage == PART_UNUSED && partition->action != ACTION_FORMAT &&
                !partition->planned) continue;
            entry.disk = disk;
            entry.partition = partition;
            entry.disk_index = disk_index;
            size_t position = count;

            while (position > 0 &&
                   partition_order(&entry, &partitions[position - 1]) < 0) {
                partitions[position] = partitions[position - 1];
                --position;
            }
            partitions[position] = entry;
            ++count;
        }
    }
    return count;
}

static const char *partition_field(const PartitionPlan *partition, PartitionField field)
{
    Filesystem filesystem;

    switch (field) {
    case FIELD_DEVICE:
        return partition->device;
    case FIELD_USAGE:
        return usage_name(partition->usage);
    case FIELD_ACTION:
        return action_name(partition->action);
    case FIELD_FILESYSTEM:
        filesystem = partition->action == ACTION_FORMAT
                         ? partition->target_fs
                         : filesystem_from_name(partition->current_fs);
        return filesystem_name(filesystem);
    case FIELD_F2FS_MODE:
        return f2fs_mode_name(partition->f2fs_mode);
    case FIELD_MOUNTPOINT:
        return partition_mountpoint(partition->usage);
    case FIELD_FS_UUID:
        return partition->fs_uuid;
    case FIELD_PART_UUID:
        return partition->part_uuid;
    case FIELD_PART_TYPE:
        return partition->part_type;
    }
    return "";
}

static void emit_partition_number_array(ScriptWriter *writer, const char *name,
                                        const PartitionRef *partitions,
                                        size_t count, PartitionNumberField field)
{
    writer_printf(writer, "%s=(\n", name);
    for (size_t index = 0; index < count; ++index) {
        uint64_t value;
        switch (field) {
        case NUMBER_PARTITION:
            value = partitions[index].partition->number;
            break;
        case NUMBER_START_SECTOR:
            value = partitions[index].partition->start_sector;
            break;
        case NUMBER_SIZE_BYTES:
            value = partitions[index].partition->size_bytes;
            break;
        default:
            value = 0;
            break;
        }
        writer_printf(writer, "    '%" PRIu64 "'\n", value);
    }
    writer_puts(writer, ")\n");
}

static void emit_package_values(ScriptWriter *writer, const PackageList *packages)
{
    for (size_t index = 0; index < packages->count; ++index) {
        char *quoted = shell_quote(packages->values[index]);
        if (quoted == NULL) {
            writer->ok = false;
            return;
        }
        writer_printf(writer, "    %s\n", quoted);
        free(quoted);
    }
}

static void emit_package_group(ScriptWriter *writer, const PackageConfig *config,
                               PackageGroup group)
{
    const PackageList *packages = packages_get(config, group);

    if (packages == NULL) {
        writer->ok = false;
        return;
    }
    emit_package_values(writer, packages);
}

static PackageGroup selected_kernel_group(Kernel kernel)
{
    switch (kernel) {
    case KERNEL_LINUX: return PKG_KERNEL_LINUX;
    case KERNEL_LTS: return PKG_KERNEL_LTS;
    case KERNEL_ZEN: return PKG_KERNEL_ZEN;
    case KERNEL_HARDENED: return PKG_KERNEL_HARDENED;
    }
    return PKG_KERNEL_LINUX;
}

void emit_package_array(ScriptWriter *writer, const char *name,
                               const PackageConfig *config, PackageGroup group)
{
    writer_printf(writer, "%s=(\n", name);
    emit_package_group(writer, config, group);
    writer_puts(writer, ")\n");
}

static void emit_required_packages(ScriptWriter *writer, const InstallPlan *plan,
                                   const PackageConfig *config)
{
    PackageGroup kernel_group = selected_kernel_group(plan->system.kernel);

    writer_puts(writer, "REQUIRED_PACKAGES=(\n");
    emit_package_group(writer, config, PKG_BOOTSTRAP);
    emit_package_group(writer, config, PKG_CORE);
    emit_package_group(writer, config, kernel_group);
    if (plan->system.platform == PLATFORM_INTEL) {
        emit_package_group(writer, config, PKG_PLATFORM_INTEL);
    } else if (plan->system.platform == PLATFORM_AMD) {
        emit_package_group(writer, config, PKG_PLATFORM_AMD);
    }
    if (plan->system.laptop) {
        emit_package_group(writer, config, PKG_LAPTOP_FIRMWARE);
        emit_package_group(writer, config, PKG_LAPTOP_TOOLS);
    }
    if (plan->system.intel_graphics) emit_package_group(writer, config, PKG_INTEL_GRAPHICS);
    if (plan->system.nvidia_graphics) emit_package_group(writer, config, PKG_NVIDIA_GRAPHICS);
    if (plan->system.bluetooth) emit_package_group(writer, config, PKG_BLUETOOTH);
    switch (plan->system.desktop) {
    case DESKTOP_KDE:
        emit_package_group(writer, config, PKG_KDE);
        if (plan->system.desktop_recommended)
            emit_package_group(writer, config, PKG_KDE_RECOMMENDED);
        if (plan->system.chinese_input) emit_package_group(writer, config, PKG_FCITX);
        break;
    case DESKTOP_GNOME:
        emit_package_group(writer, config, PKG_GNOME);
        if (plan->system.laptop) emit_package_group(writer, config, PKG_GNOME_LAPTOP);
        if (plan->system.desktop_recommended)
            emit_package_group(writer, config, PKG_GNOME_RECOMMENDED);
        if (plan->system.chinese_input) emit_package_group(writer, config, PKG_IBUS);
        break;
    case DESKTOP_HYPRLAND:
        emit_package_group(writer, config, PKG_HYPRLAND);
        if (plan->system.chinese_input) emit_package_group(writer, config, PKG_FCITX);
        break;
    case DESKTOP_NONE:
        break;
    }
    emit_package_group(writer, config, PKG_FONTS);
    if (plan->system.firewall) emit_package_group(writer, config, PKG_FIREWALL);
    if (plan->system.printer) emit_package_group(writer, config, PKG_PRINTER);
    if (plan->system.archive_tools) emit_package_group(writer, config, PKG_ARCHIVE_TOOLS);
    if (plan->system.terminal_tools) emit_package_group(writer, config, PKG_TERMINAL_TOOLS);
    if (plan->system.extra_tools) emit_package_group(writer, config, PKG_EXTRA_TOOLS);
    if (plan->system.desktop_apps) emit_package_group(writer, config, PKG_DESKTOP_APPS);
    if (plan->system.secure_boot) emit_package_group(writer, config, PKG_SECURE_BOOT_LIVE);
    writer_puts(writer, ")\n");
}

static void emit_partition_array(ScriptWriter *writer, const char *name,
                                 const PartitionRef *partitions,
                                 size_t count, PartitionField field)
{
    writer_printf(writer, "%s=(\n", name);
    for (size_t index = 0; index < count; ++index) {
        const char *value = partition_field(partitions[index].partition, field);
        char *quoted = shell_quote(value);

        if (quoted == NULL) {
            writer->ok = false;
            return;
        }
        writer_printf(writer, "    %s\n", quoted);
        free(quoted);
    }
    writer_puts(writer, ")\n");
}

static const char *storage_mode_value(StorageMode mode)
{
    switch (mode) {
    case STORAGE_EXISTING:
        return "existing";
    case STORAGE_AUTO_ROOT_SWAP:
        return "auto-root-swap";
    case STORAGE_AUTO_HOME_SWAP:
        return "auto-home-swap";
    case STORAGE_AUTO_ROOT_ONLY:
        return "auto-root-only";
    case STORAGE_AUTO_DATA:
        return "auto-data";
    }
    return "invalid";
}

static uint64_t disk_partition_size_mib(const DiskPlan *disk, PartitionUsage usage)
{
    for (size_t index = 0; index < disk->partition_count; ++index) {
        if (disk->partitions[index].usage == usage) {
            return disk->partitions[index].size_bytes / MIB;
        }
    }
    return 0;
}

static uint64_t flexible_size_mib(const DiskPlan *disk, PartitionUsage usage)
{
    uint64_t size = disk_partition_size_mib(disk, usage);

    /* Leave room for the aligned first sector and the backup GPT header. */
    return size > UINT64_C(2) ? size - UINT64_C(2) : 0;
}

static void emit_disk_string_array(ScriptWriter *writer, const char *name,
                                   const InstallPlan *plan, unsigned field)
{
    writer_printf(writer, "%s=(\n", name);
    for (size_t index = 0; index < plan->storage.disk_count; ++index) {
        const DiskPlan *disk = &plan->storage.disks[index];
        const char *value = field == 0 ? disk->path : field == 1 ? disk->model :
                            field == 2 ? disk->serial : field == 3 ? disk->partition_table :
                            storage_mode_value(disk->mode);
        char *quoted = shell_quote(value);
        if (quoted == NULL) { writer->ok = false; return; }
        writer_printf(writer, "    %s\n", quoted);
        free(quoted);
    }
    writer_puts(writer, ")\n");
}

static void emit_disk_number_array(ScriptWriter *writer, const char *name,
                                   const InstallPlan *plan, unsigned field)
{
    writer_printf(writer, "%s=(\n", name);
    for (size_t index = 0; index < plan->storage.disk_count; ++index) {
        const DiskPlan *disk = &plan->storage.disks[index];
        uint64_t value;
        if (field == 0) value = disk->size_bytes;
        else if (field == 1) value = disk_partition_size_mib(disk, PART_BOOT);
        else if (field == 2) value = disk->mode == STORAGE_AUTO_ROOT_SWAP
                                      ? flexible_size_mib(disk, PART_ROOT)
                                      : disk_partition_size_mib(disk, PART_ROOT);
        else if (field == 3) value = disk->mode == STORAGE_AUTO_HOME_SWAP
                                      ? flexible_size_mib(disk, PART_HOME)
                                      : disk_partition_size_mib(disk, PART_HOME);
        else value = disk_partition_size_mib(disk, PART_SWAP);
        writer_printf(writer, "    '%" PRIu64 "'\n", value);
    }
    writer_puts(writer, ")\n");
}

static void emit_partition_disk_indexes(ScriptWriter *writer,
                                        const PartitionRef *partitions, size_t count)
{
    writer_puts(writer, "PART_DISK_INDEXES=(\n");
    for (size_t index = 0; index < count; ++index) {
        writer_printf(writer, "    '%zu'\n", partitions[index].disk_index);
    }
    writer_puts(writer, ")\n");
}

bool emit_header_and_plan(ScriptWriter *writer, const InstallPlan *plan,
                          const PackageConfig *packages)
{
    PartitionRef used[AI_MAX_PLAN_DISKS * AI_MAX_PARTITIONS];
    const PartitionPlan *root = find_partition(plan, PART_ROOT);
    const PartitionPlan *boot = find_partition(plan, PART_BOOT);
    const DiskPlan *root_disk = find_partition_disk(plan, root);
    size_t used_count = collect_actionable_partitions(plan, used);
    const char *kernel = kernel_name(plan->system.kernel);
    char kernel_image[AI_TEXT_LEN];
    char initramfs_image[AI_TEXT_LEN];
    char fallback_image[AI_TEXT_LEN];

    (void)snprintf(kernel_image, sizeof(kernel_image), "vmlinuz-%s", kernel);
    (void)snprintf(initramfs_image, sizeof(initramfs_image), "initramfs-%s.img", kernel);
    (void)snprintf(fallback_image, sizeof(fallback_image),
                   "initramfs-%s-fallback.img", kernel);

    writer_write(writer, preamble_template, sizeof(preamble_template));
    if (!emit_assignment(writer, "TARGET_DISK", root_disk == NULL ? "" : root_disk->path) ||
        !emit_assignment(writer, "TARGET_TIMEZONE", plan->system.timezone) ||
        !emit_assignment(writer, "ROOT_DEVICE", root == NULL ? "" : root->device) ||
        !emit_assignment(writer, "BOOT_DEVICE", boot == NULL ? "" : boot->device) ||
        !emit_assignment(writer, "KERNEL_IMAGE", kernel_image) ||
        !emit_assignment(writer, "INITRAMFS_IMAGE", initramfs_image) ||
        !emit_assignment(writer, "FALLBACK_IMAGE", fallback_image)) {
        return false;
    }
    emit_disk_string_array(writer, "INSTALL_DISKS", plan, 0);
    emit_disk_string_array(writer, "DISK_MODELS", plan, 1);
    emit_disk_string_array(writer, "DISK_SERIALS", plan, 2);
    emit_disk_string_array(writer, "DISK_PTTYPES", plan, 3);
    emit_disk_string_array(writer, "DISK_MODES", plan, 4);
    emit_disk_number_array(writer, "DISK_SIZES", plan, 0);
    emit_disk_number_array(writer, "DISK_EFI_SIZE_MIB", plan, 1);
    emit_disk_number_array(writer, "DISK_ROOT_SIZE_MIB", plan, 2);
    emit_disk_number_array(writer, "DISK_HOME_SIZE_MIB", plan, 3);
    emit_disk_number_array(writer, "DISK_SWAP_SIZE_MIB", plan, 4);
    emit_boolean(writer, "USE_LOCAL_MIRROR", plan->system.local_mirror);
    emit_boolean(writer, "CREATE_EFI_ENTRY", plan->system.create_efi_entry);
    emit_partition_disk_indexes(writer, used, used_count);
    emit_partition_array(writer, "PART_DEVICES", used, used_count, FIELD_DEVICE);
    emit_partition_array(writer, "PART_USAGES", used, used_count, FIELD_USAGE);
    emit_partition_array(writer, "PART_ACTIONS", used, used_count, FIELD_ACTION);
    emit_partition_array(writer, "PART_FILESYSTEMS", used, used_count, FIELD_FILESYSTEM);
    emit_partition_array(writer, "PART_F2FS_MODES", used, used_count, FIELD_F2FS_MODE);
    emit_partition_array(writer, "PART_MOUNTPOINTS", used, used_count, FIELD_MOUNTPOINT);
    emit_partition_array(writer, "PART_FS_UUIDS", used, used_count, FIELD_FS_UUID);
    emit_partition_array(writer, "PART_UUIDS", used, used_count, FIELD_PART_UUID);
    emit_partition_array(writer, "PART_TYPES", used, used_count, FIELD_PART_TYPE);
    emit_partition_number_array(writer, "PART_NUMBERS", used, used_count, NUMBER_PARTITION);
    emit_partition_number_array(writer, "PART_START_SECTORS", used, used_count,
                                NUMBER_START_SECTOR);
    emit_partition_number_array(writer, "PART_SIZES", used, used_count, NUMBER_SIZE_BYTES);
    emit_package_array(writer, "BOOTSTRAP_PACKAGES", packages, PKG_BOOTSTRAP);
    emit_package_array(writer, "KERNEL_PACKAGES", packages,
                       selected_kernel_group(plan->system.kernel));
    if (plan->system.platform == PLATFORM_INTEL) {
        emit_package_array(writer, "PLATFORM_PACKAGES", packages, PKG_PLATFORM_INTEL);
    } else if (plan->system.platform == PLATFORM_AMD) {
        emit_package_array(writer, "PLATFORM_PACKAGES", packages, PKG_PLATFORM_AMD);
    } else {
        writer_puts(writer, "PLATFORM_PACKAGES=(\n)\n");
    }
    emit_package_array(writer, "LAPTOP_FIRMWARE_PACKAGES", packages,
                       PKG_LAPTOP_FIRMWARE);
    emit_package_array(writer, "LIVE_SIGNING_PACKAGES", packages,
                       PKG_SECURE_BOOT_LIVE);
    emit_required_packages(writer, plan, packages);
    writer_puts(writer, "\n");
    return writer->ok;
}
