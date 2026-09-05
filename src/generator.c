#define _POSIX_C_SOURCE 200809L

#include "generator.h"

#include "util.h"

#include <errno.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define MIB UINT64_C(1048576)

typedef struct {
    FILE *file;
    bool ok;
} ScriptWriter;

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

static void writer_printf(ScriptWriter *writer, const char *format, ...)
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((format(printf, 2, 3)))
#endif
    ;

static void writer_puts(ScriptWriter *writer, const char *text)
{
    if (writer->ok && fputs(text, writer->file) == EOF) {
        writer->ok = false;
    }
}

static void writer_printf(ScriptWriter *writer, const char *format, ...)
{
    va_list arguments;

    if (!writer->ok) {
        return;
    }
    va_start(arguments, format);
    if (vfprintf(writer->file, format, arguments) < 0) {
        writer->ok = false;
    }
    va_end(arguments);
}

static bool emit_assignment(ScriptWriter *writer, const char *name, const char *value)
{
    char *quoted = shell_quote(value);

    if (quoted == NULL) {
        writer->ok = false;
        return false;
    }
    writer_printf(writer, "readonly %s=%s\n", name, quoted);
    free(quoted);
    return writer->ok;
}

static void emit_boolean(ScriptWriter *writer, const char *name, bool value)
{
    writer_printf(writer, "readonly %s=%s\n", name, value ? "true" : "false");
}

static const PartitionPlan *find_partition(const InstallPlan *plan, PartitionUsage usage)
{
    for (size_t index = 0; index < plan->storage.partition_count; ++index) {
        const PartitionPlan *partition = &plan->storage.partitions[index];

        if (partition->usage == usage) {
            return partition;
        }
    }
    return NULL;
}

static int partition_order(const PartitionPlan *left, const PartitionPlan *right)
{
    const char *left_mount;
    const char *right_mount;

    if (left->usage == PART_ROOT && right->usage != PART_ROOT) {
        return -1;
    }
    if (right->usage == PART_ROOT && left->usage != PART_ROOT) {
        return 1;
    }
    if (left->usage == PART_SWAP && right->usage != PART_SWAP) {
        return 1;
    }
    if (right->usage == PART_SWAP && left->usage != PART_SWAP) {
        return -1;
    }
    left_mount = partition_mountpoint(left->usage);
    right_mount = partition_mountpoint(right->usage);
    return strcmp(left_mount, right_mount);
}

static size_t collect_used_partitions(const InstallPlan *plan,
                                      const PartitionPlan **partitions)
{
    size_t count = 0;

    for (size_t index = 0; index < plan->storage.partition_count; ++index) {
        const PartitionPlan *partition = &plan->storage.partitions[index];

        if (partition->usage != PART_UNUSED) {
            size_t position = count;

            while (position > 0 &&
                   partition_order(partition, partitions[position - 1]) < 0) {
                partitions[position] = partitions[position - 1];
                --position;
            }
            partitions[position] = partition;
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
                                        const PartitionPlan *const *partitions,
                                        size_t count, PartitionNumberField field)
{
    writer_printf(writer, "%s=(\n", name);
    for (size_t index = 0; index < count; ++index) {
        uint64_t value;
        switch (field) {
        case NUMBER_PARTITION:
            value = partitions[index]->number;
            break;
        case NUMBER_START_SECTOR:
            value = partitions[index]->start_sector;
            break;
        case NUMBER_SIZE_BYTES:
            value = partitions[index]->size_bytes;
            break;
        default:
            value = 0;
            break;
        }
        writer_printf(writer, "    '%" PRIu64 "'\n", value);
    }
    writer_puts(writer, ")\n");
}

static void emit_package_values(ScriptWriter *writer, const char *const packages[], size_t count)
{
    for (size_t index = 0; index < count; ++index) {
        char *quoted = shell_quote(packages[index]);
        if (quoted == NULL) {
            writer->ok = false;
            return;
        }
        writer_printf(writer, "    %s\n", quoted);
        free(quoted);
    }
}

#define EMIT_PACKAGES(writer, values) \
    emit_package_values((writer), (values), sizeof(values) / sizeof((values)[0]))

static void emit_required_packages(ScriptWriter *writer, const InstallPlan *plan)
{
    static const char *const base[] = {
        "base", "base-devel", "linux-firmware", "dosfstools", "xfsprogs",
        "f2fs-tools", "exfatprogs", "btrfs-progs", "ntfsprogs", "nano", "vi",
        "man-db", "man-pages", "texinfo", "zsh", "zsh-completions",
        "zsh-autosuggestions", "zsh-syntax-highlighting", "grml-zsh-config",
        "networkmanager", "iwd", "dhcpcd", "dhclient", "efivar", "efitools",
        "efibootmgr", "sbsigntools", "mokutil"
    };
    static const char *const intel_graphics[] = {
        "vulkan-intel", "intel-media-driver", "intel-gpu-tools"
    };
    static const char *const nvidia_graphics[] = {
        "nvidia-open-dkms", "nvidia-utils", "vdpauinfo"
    };
    static const char *const bluetooth[] = {"bluez", "bluez-utils", "wireless-regdb"};
    static const char *const kde[] = {"plasma", "sddm-kcm"};
    static const char *const kde_recommended[] = {
        "konsole", "dolphin", "ark", "kate", "partitionmanager", "filelight",
        "kcalc", "gwenview", "okular", "kcharselect", "ksystemlog", "kompare",
        "kid3", "haruna"
    };
    static const char *const fcitx[] = {"fcitx5-im", "fcitx5-chinese-addons"};
    static const char *const gnome[] = {"gnome", "gdm"};
    static const char *const gnome_recommended[] = {
        "dconf-editor", "gnome-tweaks", "file-roller", "gnome-shell-extension-appindicator"
    };
    static const char *const ibus[] = {"ibus", "ibus-libpinyin"};
    static const char *const hyprland[] = {
        "uwsm", "greetd", "greetd-regreet", "hyprland", "hyprpolkitagent",
        "hyprpaper", "hyprpicker", "hyprshutdown", "waybar", "cliphist", "wofi",
        "playerctl", "brightnessctl", "libnotify", "pavucontrol",
        "network-manager-applet", "blueman", "mako", "pipewire", "pipewire-jack",
        "pipewire-alsa", "pipewire-pulse", "wireplumber", "xdg-desktop-portal",
        "xdg-desktop-portal-hyprland", "xdg-desktop-portal-gtk", "xdg-user-dirs",
        "wl-clipboard", "grim", "slurp", "swayimg", "kvantum", "kvantum-qt5",
        "nwg-look", "qt5-wayland", "qt6-wayland", "qt5ct", "qt6ct", "thunar",
        "gvfs", "gvfs-smb", "gvfs-mtp", "tumbler", "ffmpegthumbnailer",
        "file-roller", "thunar-archive-plugin", "thunar-media-tags-plugin",
        "papirus-icon-theme", "materia-gtk-theme", "kvantum-theme-materia"
    };
    static const char *const fonts[] = {
        "noto-fonts", "noto-fonts-cjk", "noto-fonts-emoji", "noto-fonts-extra",
        "ttf-sarasa-gothic", "ttf-jetbrains-mono", "ttf-dejavu",
        "ttf-nerd-fonts-symbols", "ttf-nerd-fonts-symbols-mono"
    };
    static const char *const archive_tools[] = {"unrar", "7zip", "zip", "unzip"};
    static const char *const terminal_tools[] = {
        "git", "openssh", "htop", "nvtop", "tmux", "lynx", "wget", "aria2",
        "rsync", "usbutils", "cmus"
    };
    static const char *const extra_tools[] = {
        "kitty", "neovim", "neovide", "lua51", "luarocks", "fd", "ripgrep",
        "wl-clipboard", "npm", "vim", "mpv"
    };
    static const char *const desktop_apps[] = {
        "chromium", "thunderbird", "libreoffice-fresh", "gimp"
    };
    const char *kernel_values[2];
    char headers[AI_TEXT_LEN];

    (void)snprintf(headers, sizeof(headers), "%s-headers", kernel_name(plan->system.kernel));
    kernel_values[0] = kernel_name(plan->system.kernel);
    kernel_values[1] = headers;
    writer_puts(writer, "REQUIRED_PACKAGES=(\n");
    EMIT_PACKAGES(writer, base);
    emit_package_values(writer, kernel_values, 2);
    if (plan->system.platform == PLATFORM_INTEL) {
        static const char *const values[] = {"intel-ucode"};
        EMIT_PACKAGES(writer, values);
    } else if (plan->system.platform == PLATFORM_AMD) {
        static const char *const values[] = {"amd-ucode"};
        EMIT_PACKAGES(writer, values);
    }
    if (plan->system.laptop) {
        static const char *const values[] = {"sof-firmware", "tlp"};
        EMIT_PACKAGES(writer, values);
    }
    if (plan->system.intel_graphics) EMIT_PACKAGES(writer, intel_graphics);
    if (plan->system.nvidia_graphics) EMIT_PACKAGES(writer, nvidia_graphics);
    if (plan->system.bluetooth) EMIT_PACKAGES(writer, bluetooth);
    switch (plan->system.desktop) {
    case DESKTOP_KDE:
        EMIT_PACKAGES(writer, kde);
        if (plan->system.desktop_recommended) EMIT_PACKAGES(writer, kde_recommended);
        if (plan->system.chinese_input) EMIT_PACKAGES(writer, fcitx);
        break;
    case DESKTOP_GNOME:
        EMIT_PACKAGES(writer, gnome);
        if (plan->system.laptop) {
            static const char *const values[] = {"tlp-pd"};
            EMIT_PACKAGES(writer, values);
        }
        if (plan->system.desktop_recommended) EMIT_PACKAGES(writer, gnome_recommended);
        if (plan->system.chinese_input) EMIT_PACKAGES(writer, ibus);
        break;
    case DESKTOP_HYPRLAND:
        EMIT_PACKAGES(writer, hyprland);
        if (plan->system.chinese_input) EMIT_PACKAGES(writer, fcitx);
        break;
    case DESKTOP_NONE:
        break;
    }
    EMIT_PACKAGES(writer, fonts);
    if (plan->system.firewall) {
        static const char *const values[] = {"firewalld"};
        EMIT_PACKAGES(writer, values);
    }
    if (plan->system.printer) {
        static const char *const values[] = {"cups", "system-config-printer"};
        EMIT_PACKAGES(writer, values);
    }
    if (plan->system.archive_tools) EMIT_PACKAGES(writer, archive_tools);
    if (plan->system.terminal_tools) EMIT_PACKAGES(writer, terminal_tools);
    if (plan->system.extra_tools) EMIT_PACKAGES(writer, extra_tools);
    if (plan->system.desktop_apps) EMIT_PACKAGES(writer, desktop_apps);
    writer_puts(writer, ")\n");
}

#undef EMIT_PACKAGES

static void emit_partition_array(ScriptWriter *writer, const char *name,
                                 const PartitionPlan *const *partitions,
                                 size_t count, PartitionField field)
{
    writer_printf(writer, "%s=(\n", name);
    for (size_t index = 0; index < count; ++index) {
        const char *value = partition_field(partitions[index], field);
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
    }
    return "invalid";
}

static uint64_t partition_size_mib(const InstallPlan *plan, PartitionUsage usage)
{
    const PartitionPlan *partition = find_partition(plan, usage);

    return partition == NULL ? 0 : partition->size_bytes / MIB;
}

static uint64_t flexible_size_mib(const InstallPlan *plan, PartitionUsage usage)
{
    uint64_t size = partition_size_mib(plan, usage);

    /* Leave room for the aligned first sector and the backup GPT header. */
    return size > UINT64_C(2) ? size - UINT64_C(2) : 0;
}

static bool emit_header_and_plan(ScriptWriter *writer, const InstallPlan *plan)
{
    const PartitionPlan *used[AI_MAX_PARTITIONS];
    const PartitionPlan *root = find_partition(plan, PART_ROOT);
    const PartitionPlan *boot = find_partition(plan, PART_BOOT);
    size_t used_count = collect_used_partitions(plan, used);
    const char *kernel = kernel_name(plan->system.kernel);
    const char *microcode = "";
    char kernel_image[AI_TEXT_LEN];
    char initramfs_image[AI_TEXT_LEN];
    char fallback_image[AI_TEXT_LEN];

    if (plan->system.platform == PLATFORM_INTEL) {
        microcode = "intel-ucode";
    } else if (plan->system.platform == PLATFORM_AMD) {
        microcode = "amd-ucode";
    }

    (void)snprintf(kernel_image, sizeof(kernel_image), "vmlinuz-%s", kernel);
    (void)snprintf(initramfs_image, sizeof(initramfs_image), "initramfs-%s.img", kernel);
    (void)snprintf(fallback_image, sizeof(fallback_image),
                   "initramfs-%s-fallback.img", kernel);

    writer_puts(writer,
                "#!/usr/bin/bash\n"
                "set -Eeuo pipefail\n"
                "PATH='/usr/bin'\n"
                "export PATH\n"
                "readonly PATH\n"
                "umask 022\n\n"
                "readonly SCRIPT_DIR=\"$(cd -- \"$(dirname -- \"${BASH_SOURCE[0]}\")\" && pwd -P)\"\n"
                "readonly ASSET_DIR=\"${ARCH_INSTALL_ASSET_DIR:-$SCRIPT_DIR/live}\"\n"
                "readonly TARGET_ROOT='/mnt'\n"
                "LOG_FILE=${ARCH_INSTALL_LOG:-}\n");
    if (!emit_assignment(writer, "TARGET_DISK", plan->storage.disk_path) ||
        !emit_assignment(writer, "EXPECTED_MODEL", plan->storage.disk_model) ||
        !emit_assignment(writer, "EXPECTED_SERIAL", plan->storage.disk_serial) ||
        !emit_assignment(writer, "EXPECTED_PTTYPE", plan->storage.partition_table) ||
        !emit_assignment(writer, "TARGET_TIMEZONE", plan->system.timezone) ||
        !emit_assignment(writer, "STORAGE_MODE", storage_mode_value(plan->storage.mode)) ||
        !emit_assignment(writer, "ROOT_DEVICE", root == NULL ? "" : root->device) ||
        !emit_assignment(writer, "BOOT_DEVICE", boot == NULL ? "" : boot->device) ||
        !emit_assignment(writer, "KERNEL_PACKAGE", kernel) ||
        !emit_assignment(writer, "KERNEL_IMAGE", kernel_image) ||
        !emit_assignment(writer, "INITRAMFS_IMAGE", initramfs_image) ||
        !emit_assignment(writer, "FALLBACK_IMAGE", fallback_image) ||
        !emit_assignment(writer, "MICROCODE_PACKAGE", microcode)) {
        return false;
    }
    writer_printf(writer, "readonly EXPECTED_SIZE=%" PRIu64 "\n",
                  plan->storage.disk_size_bytes);
    writer_printf(writer, "readonly AUTO_EFI_SIZE_MIB=%" PRIu64 "\n",
                  partition_size_mib(plan, PART_BOOT));
    writer_printf(writer, "readonly AUTO_ROOT_SIZE_MIB=%" PRIu64 "\n",
                  plan->storage.mode == STORAGE_AUTO_ROOT_SWAP
                      ? flexible_size_mib(plan, PART_ROOT)
                      : partition_size_mib(plan, PART_ROOT));
    writer_printf(writer, "readonly AUTO_HOME_SIZE_MIB=%" PRIu64 "\n",
                  plan->storage.mode == STORAGE_AUTO_HOME_SWAP
                      ? flexible_size_mib(plan, PART_HOME)
                      : partition_size_mib(plan, PART_HOME));
    writer_printf(writer, "readonly AUTO_SWAP_SIZE_MIB=%" PRIu64 "\n",
                  partition_size_mib(plan, PART_SWAP));
    emit_boolean(writer, "USE_LOCAL_MIRROR", plan->system.local_mirror);
    emit_boolean(writer, "CREATE_EFI_ENTRY", plan->system.create_efi_entry);
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
    emit_required_packages(writer, plan);
    writer_puts(writer, "\n");
    return writer->ok;
}

static void emit_outer_runtime(ScriptWriter *writer)
{
    writer_puts(writer,
        "WORK_DIR=''\n"
        "TARGET_MOUNTED=false\n"
        "LOCAL_MIRROR_MOUNTED=false\n"
        "TARGET_LOCAL_MIRROR_MOUNTED=false\n"
        "readonly TARGET_LOCAL_MIRROR=\"$TARGET_ROOT/var/cache/arch-install-repo\"\n"
        "HOST_PACMAN_CHANGED=false\n"
        "TARGET_CONFIG_FINALIZED=false\n"
        "INSTALL_SUCCEEDED=false\n"
        "LOCAL_MIRROR_SOURCE=''\n"
        "LOCAL_MIRROR_UUID=''\n"
        "LOCAL_MIRROR_PARENT=''\n"
        "LOCAL_MIRROR_PARENT_SERIAL=''\n"
        "LOCAL_MIRROR_SIZE=''\n"
        "KEEP_PROBE_MOUNT=''\n"
        "KEEP_PROBE_SOURCE=''\n"
        "KEEP_PROBE_ACTIVE=false\n"
        "SECURE_BOOT_ASSET_SNAPSHOT=''\n"
        "SECURE_BOOT_SNAPSHOT_MOUNTED=false\n"
        "SECURE_BOOT_SIGNING_COMPLETE=false\n"
        "CONSOLE_FD=''\n"
        "LOG_FD=''\n"
        "LOG_TEE_PID=''\n"
        "SWAPS_TO_DISABLE=()\n"
        "SECURE_BOOT_STAGED_FILES=()\n\n"
        "die() {\n"
        "    printf 'ERROR: %s\\n' \"$*\" >&2\n"
        "    exit 1\n"
        "}\n\n"
        "phase() {\n"
        "    printf '\\n==> %s\\n' \"$*\"\n"
        "}\n\n");
    writer_puts(writer,
        "cleanup() {\n"
        "    local status=$?\n"
        "    local index logger_watchdog query_status cleanup_failed=false\n"
        "    local target_active=false mirror_active=false snapshot_active=false active_swaps=''\n"
        "    local snapshot_remove_safe=true\n"
        "    local mounted_source=''\n"
        "    trap - EXIT INT TERM HUP\n"
        "    set +e\n"
        "    if command -v findmnt >/dev/null 2>&1; then\n"
        "        if [[ \"$KEEP_PROBE_ACTIVE\" == true && -n \"$KEEP_PROBE_MOUNT\" ]]; then\n"
        "            if mounted_source=$(findmnt -rn --mountpoint \"$KEEP_PROBE_MOUNT\" -o SOURCE); then\n"
        "                if [[ \"$mounted_source\" != \"$KEEP_PROBE_SOURCE\" ]]; then\n"
        "                    printf 'WARNING: refusing to clean a foreign KEEP probe mount.\\n' >&2\n"
        "                    cleanup_failed=true\n"
        "                elif ! umount -- \"$KEEP_PROBE_MOUNT\"; then\n"
        "                    printf 'WARNING: failed to unmount the KEEP probe.\\n' >&2\n"
        "                    cleanup_failed=true\n"
        "                fi\n"
        "            else\n"
        "                query_status=$?\n"
        "                if [[ \"$query_status\" -ne 1 ]]; then\n"
        "                    printf 'WARNING: could not inspect the KEEP probe during cleanup.\\n' >&2\n"
        "                    cleanup_failed=true\n"
        "                fi\n"
        "            fi\n"
        "        fi\n"
        "        if [[ \"$TARGET_MOUNTED\" == true ]]; then\n"
        "            if mounted_source=$(findmnt -rn --mountpoint \"$TARGET_ROOT\" -o SOURCE); then\n"
        "                if [[ \"$mounted_source\" == \"$ROOT_DEVICE\" ]]; then\n"
        "                    target_active=true\n"
        "                else\n"
        "                    printf 'WARNING: refusing to clean a foreign mount at %s.\\n' \"$TARGET_ROOT\" >&2\n"
        "                    cleanup_failed=true\n"
        "                fi\n"
        "            else\n"
        "                query_status=$?\n"
        "                if [[ \"$query_status\" -ne 1 ]]; then\n"
        "                    printf 'WARNING: could not inspect the target mount during cleanup.\\n' >&2\n"
        "                    cleanup_failed=true\n"
        "                fi\n"
        "            fi\n"
        "        fi\n");
    writer_puts(writer,
        "        if [[ \"$LOCAL_MIRROR_MOUNTED\" == true ]]; then\n"
        "            if mounted_source=$(findmnt -rn --mountpoint /run/media/root/F2FS-DATA -o SOURCE); then\n"
        "                if [[ \"$mounted_source\" == \"$LOCAL_MIRROR_SOURCE\" ]]; then\n"
        "                    mirror_active=true\n"
        "                else\n"
        "                    printf 'WARNING: refusing to clean a foreign local-mirror mount.\\n' >&2\n"
        "                    cleanup_failed=true\n"
        "                fi\n"
        "            else\n"
        "                query_status=$?\n"
        "                if [[ \"$query_status\" -ne 1 ]]; then\n"
        "                    printf 'WARNING: could not inspect the local-mirror mount during cleanup.\\n' >&2\n"
        "                    cleanup_failed=true\n"
        "                fi\n"
        "            fi\n"
        "        fi\n"
        "        if [[ \"$SECURE_BOOT_SNAPSHOT_MOUNTED\" == true &&\n"
        "              -n \"$SECURE_BOOT_ASSET_SNAPSHOT\" ]]; then\n"
        "            if mounted_source=$(findmnt -rn --mountpoint \\\n"
        "                \"$SECURE_BOOT_ASSET_SNAPSHOT\" -o SOURCE,FSTYPE); then\n"
        "                if [[ \"$mounted_source\" == 'tmpfs tmpfs' ]]; then\n"
        "                    snapshot_active=true\n"
        "                else\n"
        "                    printf 'WARNING: refusing to clean a foreign Secure Boot snapshot mount.\\n' >&2\n"
        "                    cleanup_failed=true\n"
        "                    snapshot_remove_safe=false\n"
        "                fi\n"
        "            else\n"
        "                query_status=$?\n"
        "                if [[ \"$query_status\" -ne 1 ]]; then\n"
        "                    printf 'WARNING: could not inspect the Secure Boot snapshot during cleanup.\\n' >&2\n"
        "                    cleanup_failed=true\n"
        "                    snapshot_remove_safe=false\n"
        "                fi\n"
        "            fi\n"
        "        fi\n"
        "    fi\n"
        "    if [[ \"$status\" -ne 0 && \"$target_active\" == true &&\n"
        "          \"$TARGET_CONFIG_FINALIZED\" != true && -n \"$WORK_DIR\" ]]; then\n"
        "        if [[ -f \"$WORK_DIR/target-pacman.conf\" ]]; then\n"
        "            if ! cp -a -- \"$WORK_DIR/target-pacman.conf\" \"$TARGET_ROOT/etc/pacman.conf\"; then\n"
        "                printf 'WARNING: failed to restore target pacman.conf.\\n' >&2\n"
        "                cleanup_failed=true\n"
        "            fi\n"
        "        fi\n"
        "        if [[ -f \"$WORK_DIR/target-mirrorlist\" ]]; then\n"
        "            if ! cp -a -- \"$WORK_DIR/target-mirrorlist\" \"$TARGET_ROOT/etc/pacman.d/mirrorlist\"; then\n"
        "                printf 'WARNING: failed to restore target mirrorlist.\\n' >&2\n"
        "                cleanup_failed=true\n"
        "            fi\n"
        "        fi\n"
        "    fi\n");
    writer_puts(writer,
        "    if [[ \"$target_active\" == true ]]; then\n"
        "        for index in \"${!SECURE_BOOT_STAGED_FILES[@]}\"; do\n"
        "            if [[ -n \"${SECURE_BOOT_STAGED_FILES[index]}\" ]] &&\n"
        "               ! rm -f -- \"${SECURE_BOOT_STAGED_FILES[index]}\"; then\n"
        "                printf 'WARNING: failed to remove a staged Secure Boot file.\\n' >&2\n"
        "                cleanup_failed=true\n"
        "            fi\n"
        "        done\n"
        "        if ! rm -f -- \"$TARGET_ROOT/root/.arch-install-chroot.sh\"; then\n"
        "            printf 'WARNING: failed to remove temporary target files.\\n' >&2\n"
        "            cleanup_failed=true\n"
        "        fi\n"
        "        if ! umount -R -- \"$TARGET_ROOT\"; then\n"
        "            printf 'WARNING: failed to unmount %s.\\n' \"$TARGET_ROOT\" >&2\n"
        "            cleanup_failed=true\n"
        "        fi\n"
        "    fi\n"
        "    if (( ${#SWAPS_TO_DISABLE[@]} > 0 )); then\n"
        "        if ! command -v swapon >/dev/null 2>&1 ||\n"
        "           ! active_swaps=$(swapon --show=NAME --noheadings --raw 2>/dev/null); then\n"
        "            printf 'WARNING: failed to inspect swap state during cleanup.\\n' >&2\n"
        "            cleanup_failed=true\n"
        "        fi\n"
        "    fi\n"
        "    for ((index=${#SWAPS_TO_DISABLE[@]} - 1; index >= 0; --index)); do\n"
        "        if grep -Fxq -- \"${SWAPS_TO_DISABLE[index]}\" <<<\"$active_swaps\" &&\n"
        "           ! swapoff -- \"${SWAPS_TO_DISABLE[index]}\"; then\n"
        "            printf 'WARNING: failed to disable swap %s.\\n' \"${SWAPS_TO_DISABLE[index]}\" >&2\n"
        "            cleanup_failed=true\n"
        "        fi\n"
        "    done\n");
    writer_puts(writer,
        "    if [[ \"$mirror_active\" == true ]]; then\n"
        "        if ! umount -- '/run/media/root/F2FS-DATA'; then\n"
        "            printf 'WARNING: failed to unmount the local mirror.\\n' >&2\n"
        "            cleanup_failed=true\n"
        "        fi\n"
        "    fi\n"
        "    if [[ \"$HOST_PACMAN_CHANGED\" == true && -n \"$WORK_DIR\" ]]; then\n"
        "        if ! cp -a -- \"$WORK_DIR/host-pacman.conf\" /etc/pacman.conf; then\n"
        "            printf 'WARNING: failed to restore Live pacman.conf.\\n' >&2\n"
        "            cleanup_failed=true\n"
        "        fi\n"
        "        if ! cp -a -- \"$WORK_DIR/host-mirrorlist\" /etc/pacman.d/mirrorlist; then\n"
        "            printf 'WARNING: failed to restore the Live mirrorlist.\\n' >&2\n"
        "            cleanup_failed=true\n"
        "        fi\n"
        "    fi\n"
        "    if [[ \"$snapshot_active\" == true ]]; then\n"
        "        if ! umount -- \"$SECURE_BOOT_ASSET_SNAPSHOT\"; then\n"
        "            printf 'WARNING: failed to unmount the private Secure Boot snapshot.\\n' >&2\n"
        "            cleanup_failed=true\n"
        "        else\n"
        "            snapshot_active=false\n"
        "            SECURE_BOOT_SNAPSHOT_MOUNTED=false\n"
        "        fi\n"
        "    fi\n"
        "    if [[ \"$snapshot_active\" != true && \"$snapshot_remove_safe\" == true &&\n"
        "          -n \"$SECURE_BOOT_ASSET_SNAPSHOT\" &&\n"
        "          -d \"$SECURE_BOOT_ASSET_SNAPSHOT\" ]]; then\n"
        "        if ! rm -rf -- \"$SECURE_BOOT_ASSET_SNAPSHOT\"; then\n"
        "            printf 'WARNING: failed to remove the private Secure Boot snapshot.\\n' >&2\n"
        "            cleanup_failed=true\n"
        "        else\n"
        "            SECURE_BOOT_ASSET_SNAPSHOT=''\n"
        "        fi\n"
        "    fi\n"
        "    if [[ -n \"$WORK_DIR\" && -d \"$WORK_DIR\" ]]; then\n"
        "        if [[ \"$cleanup_failed\" == true ]]; then\n"
        "            printf 'WARNING: preserving recovery files in %s.\\n' \"$WORK_DIR\" >&2\n"
        "        elif ! rm -rf -- \"$WORK_DIR\"; then\n"
        "            printf 'WARNING: failed to remove temporary files: %s\\n' \"$WORK_DIR\" >&2\n"
        "            cleanup_failed=true\n"
        "        fi\n"
        "    fi\n"
        "    if [[ \"$status\" -eq 0 && \"$cleanup_failed\" == true ]]; then status=1; fi\n"
        "    if [[ -n \"$LOG_TEE_PID\" ]]; then\n"
        "        if [[ -n \"$CONSOLE_FD\" ]]; then exec 1>&$CONSOLE_FD 2>&1; fi\n"
        "        ( /usr/bin/sleep 5; kill -TERM \"$LOG_TEE_PID\" 2>/dev/null ) &\n"
        "        logger_watchdog=$!\n"
        "        if ! wait \"$LOG_TEE_PID\"; then\n"
        "            printf 'WARNING: the install logger exited unsuccessfully.\\n' >&2\n"
        "            [[ \"$status\" -ne 0 ]] || status=1\n"
        "        fi\n"
        "        kill -TERM \"$logger_watchdog\" 2>/dev/null\n"
        "        wait \"$logger_watchdog\" 2>/dev/null\n"
        "    fi\n"
        "    if [[ \"$status\" -ne 0 ]]; then\n"
        "        printf '\\nInstallation failed (exit %d). Log: %s\\n' \"$status\" \"${LOG_FILE:-unavailable}\" >&2\n"
        "    elif [[ \"$INSTALL_SUCCEEDED\" == true ]]; then\n"
        "        printf '\\nTarget filesystems were unmounted cleanly. Log: %s\\n' \"$LOG_FILE\"\n"
        "    fi\n"
        "    if [[ -n \"$LOG_FD\" ]]; then exec {LOG_FD}>&-; fi\n"
        "    if [[ -n \"$CONSOLE_FD\" ]]; then exec {CONSOLE_FD}>&-; fi\n"
        "    exit \"$status\"\n"
        "}\n\n");
    writer_puts(writer,
        "initialize_log() {\n"
        "    local previous_umask\n"
        "    exec {CONSOLE_FD}>&2 || exit 1\n"
        "    [[ -x /usr/bin/tee && -x /usr/bin/mktemp && -x /usr/bin/sleep ]] ||\n"
        "        die 'tee, mktemp, and sleep are required for logging.'\n"
        "    previous_umask=$(umask)\n"
        "    umask 077\n"
        "    if [[ -n \"$LOG_FILE\" ]]; then\n"
        "        [[ ! -e \"$LOG_FILE\" && ! -L \"$LOG_FILE\" ]] ||\n"
        "            die \"Refusing to replace existing log path: $LOG_FILE\"\n"
        "        set -o noclobber\n"
        "        if ! exec {LOG_FD}> \"$LOG_FILE\"; then\n"
        "            set +o noclobber\n"
        "            umask \"$previous_umask\"\n"
        "            die \"Cannot create log file safely: $LOG_FILE\"\n"
        "        fi\n"
        "        set +o noclobber\n"
        "    else\n"
        "        LOG_FILE=$(/usr/bin/mktemp /tmp/arch-install.XXXXXX.log) || die 'Cannot create the install log.'\n"
        "        exec {LOG_FD}>> \"$LOG_FILE\" || die 'Cannot open the install log.'\n"
        "    fi\n"
        "    umask \"$previous_umask\"\n"
        "    readonly LOG_FILE\n"
        "    exec > >(/usr/bin/tee -a \"/proc/self/fd/$LOG_FD\") 2>&1\n"
        "    LOG_TEE_PID=$!\n"
        "}\n\n"
        "trap cleanup EXIT\n"
        "trap 'exit 130' INT TERM HUP\n"
        "initialize_log\n\n"
        "require_command() {\n"
        "    command -v \"$1\" >/dev/null 2>&1 || die \"Required command not found: $1\"\n"
        "}\n\n"
        "normalize_fs() {\n"
        "    case \"${1,,}\" in\n"
        "        fat|fat16|fat32|vfat) printf 'vfat' ;;\n"
        "        *) printf '%s' \"${1,,}\" ;;\n"
        "    esac\n"
        "}\n\n"
        "ensure_node_idle() {\n"
        "    local node=$1 kernel_name holders mounted_status active_swaps holder_entry\n"
        "    if findmnt -rn -S \"$node\" >/dev/null 2>&1; then\n"
        "        die \"$node is mounted; unmount it before running this script\"\n"
        "    else\n"
        "        mounted_status=$?\n"
        "        [[ \"$mounted_status\" -eq 1 ]] || die \"Cannot inspect mounts for $node\"\n"
        "    fi\n"
        "    active_swaps=$(swapon --show=NAME --noheadings --raw 2>/dev/null) ||\n"
        "        die 'Cannot inspect active swap devices.'\n"
        "    if grep -Fxq -- \"$node\" <<<\"$active_swaps\"; then\n"
        "        die \"$node is active swap; disable it before running this script\"\n"
        "    fi\n"
        "    kernel_name=$(lsblk -dnro KNAME -- \"$node\")\n"
        "    holders=/sys/class/block/$kernel_name/holders\n"
        "    [[ -d \"$holders\" ]] || die \"Cannot inspect block-device holders for $node\"\n"
        "    holder_entry=$(find \"$holders\" -mindepth 1 -maxdepth 1 -print -quit) ||\n"
        "        die \"Cannot inspect block-device holders for $node\"\n"
        "    if [[ -n \"$holder_entry\" ]]; then\n"
        "        die \"$node is still held by an active mapped, RAID, or logical device\"\n"
        "    fi\n"
        "}\n\n");
    writer_puts(writer,
        "select_local_mirror_source() {\n"
        "    local sources=() source_text source_status ancestors filesystem parent_type\n"
        "    if source_text=$(blkid -t LABEL=F2FS-DATA -o device); then\n"
        "        [[ -z \"$source_text\" ]] || mapfile -t sources <<<\"$source_text\"\n"
        "    else\n"
        "        source_status=$?\n"
        "        [[ \"$source_status\" -eq 2 ]] || die 'Cannot inspect local-mirror labels.'\n"
        "    fi\n"
        "    [[ \"${#sources[@]}\" -eq 1 ]] ||\n"
        "        die \"Expected exactly one F2FS-DATA partition, found ${#sources[@]}\"\n"
        "    LOCAL_MIRROR_SOURCE=${sources[0]}\n"
        "    [[ -b \"$LOCAL_MIRROR_SOURCE\" ]] || die 'The local mirror source is not a block device.'\n"
        "    filesystem=$(blkid -s TYPE -o value -- \"$LOCAL_MIRROR_SOURCE\") ||\n"
        "        die 'Cannot identify the local mirror filesystem.'\n"
        "    [[ \"${filesystem,,}\" == f2fs ]] || die 'The F2FS-DATA source must use F2FS.'\n"
        "    LOCAL_MIRROR_UUID=$(blkid -s UUID -o value -- \"$LOCAL_MIRROR_SOURCE\") ||\n"
        "        die 'Cannot identify the local mirror UUID.'\n"
        "    [[ -n \"$LOCAL_MIRROR_UUID\" ]] || die 'The local mirror has no filesystem UUID.'\n"
        "    LOCAL_MIRROR_PARENT=$(lsblk -dnrpo PKNAME -- \"$LOCAL_MIRROR_SOURCE\") ||\n"
        "        die 'Cannot identify the local mirror parent disk.'\n"
        "    [[ -n \"$LOCAL_MIRROR_PARENT\" ]] || die 'The local mirror must be a disk partition.'\n"
        "    parent_type=$(lsblk -dnro TYPE -- \"$LOCAL_MIRROR_PARENT\") ||\n"
        "        die 'Cannot inspect the local mirror parent disk.'\n"
        "    [[ \"$parent_type\" == disk ]] || die 'The local mirror parent is not a whole disk.'\n"
        "    LOCAL_MIRROR_PARENT_SERIAL=$(lsblk -dno SERIAL -- \"$LOCAL_MIRROR_PARENT\" |\n"
        "        sed 's/^[[:space:]]*//;s/[[:space:]]*$//')\n"
        "    LOCAL_MIRROR_SIZE=$(blockdev --getsize64 \"$LOCAL_MIRROR_SOURCE\") ||\n"
        "        die 'Cannot determine the local mirror size.'\n"
        "    ancestors=$(lsblk -snrpo NAME -- \"$LOCAL_MIRROR_SOURCE\") ||\n"
        "        die 'Cannot inspect the local-mirror device ancestry.'\n"
        "    if grep -Fxq -- \"$TARGET_DISK\" <<<\"$ancestors\"; then\n"
        "        die 'The local mirror cannot reside on the installation disk.'\n"
        "    fi\n"
        "    ensure_node_idle \"$LOCAL_MIRROR_SOURCE\"\n"
        "}\n\n"
        "verify_local_mirror_identity() {\n"
        "    local uuid parent size label filesystem serial\n"
        "    [[ \"$USE_LOCAL_MIRROR\" == true ]] || return 0\n"
        "    [[ -b \"$LOCAL_MIRROR_SOURCE\" ]] || die 'The selected local mirror disappeared.'\n"
        "    uuid=$(blkid -s UUID -o value -- \"$LOCAL_MIRROR_SOURCE\") ||\n"
        "        die 'Cannot recheck the local mirror UUID.'\n"
        "    label=$(blkid -s LABEL -o value -- \"$LOCAL_MIRROR_SOURCE\") ||\n"
        "        die 'Cannot recheck the local mirror label.'\n"
        "    filesystem=$(blkid -s TYPE -o value -- \"$LOCAL_MIRROR_SOURCE\") ||\n"
        "        die 'Cannot recheck the local mirror filesystem.'\n"
        "    parent=$(lsblk -dnrpo PKNAME -- \"$LOCAL_MIRROR_SOURCE\") ||\n"
        "        die 'Cannot recheck the local mirror parent.'\n"
        "    size=$(blockdev --getsize64 \"$LOCAL_MIRROR_SOURCE\") ||\n"
        "        die 'Cannot recheck the local mirror size.'\n"
        "    serial=$(lsblk -dno SERIAL -- \"$parent\" | sed 's/^[[:space:]]*//;s/[[:space:]]*$//')\n"
        "    [[ \"$uuid\" == \"$LOCAL_MIRROR_UUID\" && \"$label\" == F2FS-DATA &&\n"
        "       \"${filesystem,,}\" == f2fs && \"$parent\" == \"$LOCAL_MIRROR_PARENT\" &&\n"
        "       \"$size\" == \"$LOCAL_MIRROR_SIZE\" &&\n"
        "       \"$serial\" == \"$LOCAL_MIRROR_PARENT_SERIAL\" ]] ||\n"
        "        die 'The local mirror identity changed after confirmation.'\n"
        "    ensure_node_idle \"$LOCAL_MIRROR_SOURCE\"\n"
        "}\n\n");
    writer_puts(writer,
        "verify_secure_boot_assets() {\n"
        "    local asset_root=${1:-$ASSET_DIR}\n"
        "    local file package_info key_public certificate_public crt_fingerprint cer_fingerprint\n"
        "    [[ \"$ENABLE_SECURE_BOOT\" == true ]] || return 0\n"
        "    require_command openssl\n"
        "    [[ -f \"$asset_root/shim-signed.pkg.tar.zst\" &&\n"
        "       ! -L \"$asset_root/shim-signed.pkg.tar.zst\" ]] ||\n"
        "        die \"Secure Boot requires $asset_root/shim-signed.pkg.tar.zst.\"\n"
        "    [[ -d \"$asset_root/secure-boot\" && ! -L \"$asset_root/secure-boot\" ]] ||\n"
        "        die 'The Secure Boot asset directory must be a real directory.'\n"
        "    for file in MOK.key MOK.crt MOK.cer; do\n"
        "        [[ -f \"$asset_root/secure-boot/$file\" && ! -L \"$asset_root/secure-boot/$file\" ]] ||\n"
        "            die \"Secure Boot asset missing: $asset_root/secure-boot/$file\"\n"
        "    done\n"
        "    package_info=$(pacman -Qp -- \"$asset_root/shim-signed.pkg.tar.zst\") ||\n"
        "        die 'The shim-signed asset is not a readable pacman package.'\n"
        "    [[ \"${package_info%% *}\" == shim-signed ]] || die 'The Secure Boot package is not shim-signed.'\n"
        "    key_public=$(openssl pkey -passin pass: -in \"$asset_root/secure-boot/MOK.key\" -pubout 2>/dev/null) ||\n"
        "        die 'MOK.key is invalid or requires a passphrase.'\n"
        "    certificate_public=$(openssl x509 -in \"$asset_root/secure-boot/MOK.crt\" -pubkey -noout 2>/dev/null) ||\n"
        "        die 'MOK.crt is not a valid PEM certificate.'\n"
        "    [[ \"$key_public\" == \"$certificate_public\" ]] || die 'MOK.key and MOK.crt do not match.'\n"
        "    crt_fingerprint=$(openssl x509 -in \"$asset_root/secure-boot/MOK.crt\" -noout -fingerprint -sha256 2>/dev/null)\n"
        "    cer_fingerprint=$(openssl x509 -inform DER -in \"$asset_root/secure-boot/MOK.cer\" -noout -fingerprint -sha256 2>/dev/null) ||\n"
        "        die 'MOK.cer is not a valid DER certificate.'\n"
        "    [[ \"$crt_fingerprint\" == \"$cer_fingerprint\" ]] || die 'MOK.crt and MOK.cer do not match.'\n"
        "}\n\n"
        "snapshot_secure_boot_assets() {\n"
        "    local file archive_path archive_listing\n"
        "    [[ \"$ENABLE_SECURE_BOOT\" == true ]] || return 0\n"
        "    verify_secure_boot_assets \"$ASSET_DIR\"\n"
        "    SECURE_BOOT_ASSET_SNAPSHOT=$WORK_DIR/secure-boot-snapshot\n"
        "    install -d -m 0700 \"$SECURE_BOOT_ASSET_SNAPSHOT\"\n"
        "    SECURE_BOOT_SNAPSHOT_MOUNTED=true\n"
        "    mount -t tmpfs -o nodev,nosuid,noexec,mode=0700,size=64M tmpfs \\\n"
        "        \"$SECURE_BOOT_ASSET_SNAPSHOT\"\n"
        "    install -d -m 0700 \"$SECURE_BOOT_ASSET_SNAPSHOT/secure-boot\"\n"
        "    install -m 0600 -- \"$ASSET_DIR/shim-signed.pkg.tar.zst\" \\\n"
        "        \"$SECURE_BOOT_ASSET_SNAPSHOT/shim-signed.pkg.tar.zst\"\n"
        "    for file in MOK.key MOK.crt MOK.cer; do\n"
        "        install -m 0600 -- \"$ASSET_DIR/secure-boot/$file\" \\\n"
        "            \"$SECURE_BOOT_ASSET_SNAPSHOT/secure-boot/$file\"\n"
        "    done\n"
        "    verify_secure_boot_assets \"$SECURE_BOOT_ASSET_SNAPSHOT\"\n"
        "    archive_listing=$(bsdtar -tf \"$SECURE_BOOT_ASSET_SNAPSHOT/shim-signed.pkg.tar.zst\") ||\n"
        "        die 'Cannot list the shim-signed package safely.'\n"
        "    for file in shimx64.efi mmx64.efi fbx64.efi; do\n"
        "        archive_path=usr/share/shim-signed/$file\n"
        "        if ! grep -Fxq -- \"$archive_path\" <<<\"$archive_listing\"; then\n"
        "            archive_path=./$archive_path\n"
        "            grep -Fxq -- \"$archive_path\" <<<\"$archive_listing\" ||\n"
        "                die \"shim-signed package is missing $file.\"\n"
        "        fi\n"
        "        bsdtar -xOf \"$SECURE_BOOT_ASSET_SNAPSHOT/shim-signed.pkg.tar.zst\" \\\n"
        "            \"$archive_path\" > \"$SECURE_BOOT_ASSET_SNAPSHOT/$file\" ||\n"
        "            die \"Cannot extract $file from shim-signed safely.\"\n"
        "        chmod 0600 \"$SECURE_BOOT_ASSET_SNAPSHOT/$file\"\n"
        "        [[ -s \"$SECURE_BOOT_ASSET_SNAPSHOT/$file\" ]] || die \"Extracted $file is empty.\"\n"
        "        sbverify --list \"$SECURE_BOOT_ASSET_SNAPSHOT/$file\" >/dev/null ||\n"
        "            die \"The supplied $file does not contain a valid Secure Boot signature.\"\n"
        "    done\n"
        "}\n\n");
    writer_puts(writer,
        "verify_disk_identity() {\n"
        "    local current_size current_model current_serial current_pttype current_type\n"
        "    [[ -b \"$TARGET_DISK\" ]] || die \"Target disk is not a block device: $TARGET_DISK\"\n"
        "    current_type=$(lsblk -dnro TYPE -- \"$TARGET_DISK\")\n"
        "    [[ \"$current_type\" == disk ]] || die \"Target is not a whole disk: $TARGET_DISK\"\n"
        "    [[ \"$(blockdev --getro \"$TARGET_DISK\")\" == 0 ]] || die \"Target disk is read-only: $TARGET_DISK\"\n"
        "    current_size=$(blockdev --getsize64 \"$TARGET_DISK\")\n"
        "    [[ \"$current_size\" == \"$EXPECTED_SIZE\" ]] ||\n"
        "        die \"Target disk size changed (expected $EXPECTED_SIZE, got $current_size)\"\n"
        "    current_model=$(lsblk -dno MODEL -- \"$TARGET_DISK\" | sed 's/^[[:space:]]*//;s/[[:space:]]*$//')\n"
        "    current_serial=$(lsblk -dno SERIAL -- \"$TARGET_DISK\" | sed 's/^[[:space:]]*//;s/[[:space:]]*$//')\n"
        "    current_pttype=$(lsblk -dnro PTTYPE -- \"$TARGET_DISK\")\n"
        "    if [[ -n \"$EXPECTED_MODEL\" && \"$current_model\" != \"$EXPECTED_MODEL\" ]]; then\n"
        "        die \"Target disk model changed (expected '$EXPECTED_MODEL', got '$current_model')\"\n"
        "    fi\n"
        "    if [[ -n \"$EXPECTED_SERIAL\" && \"$current_serial\" != \"$EXPECTED_SERIAL\" ]]; then\n"
        "        die \"Target disk serial changed\"\n"
        "    fi\n"
        "    if [[ \"$STORAGE_MODE\" == existing && \"${current_pttype,,}\" != \"${EXPECTED_PTTYPE,,}\" ]]; then\n"
        "        die \"Target disk partition-table type changed\"\n"
        "    fi\n"
        "}\n\n"
        "verify_existing_partition() {\n"
        "    local device=$1 expected_number=$2 expected_uuid=$3 expected_start=$4 expected_size=$5 expected_type=$6\n"
        "    local parent actual_number actual_uuid actual_start actual_size actual_type\n"
        "    [[ -b \"$device\" ]] || die \"Configured partition is missing: $device\"\n"
        "    parent=$(lsblk -dnrpo PKNAME -- \"$device\")\n"
        "    [[ \"$parent\" == \"$TARGET_DISK\" ]] ||\n"
        "        die \"$device no longer belongs to $TARGET_DISK\"\n"
        "    actual_number=$(lsblk -dnro PARTN -- \"$device\")\n"
        "    actual_uuid=$(lsblk -dnro PARTUUID -- \"$device\")\n"
        "    actual_start=$(lsblk -dnro START -- \"$device\")\n"
        "    actual_size=$(blockdev --getsize64 \"$device\")\n"
        "    actual_type=$(lsblk -dnro PARTTYPE -- \"$device\")\n"
        "    [[ \"$actual_number\" == \"$expected_number\" ]] || die \"Partition number changed for $device\"\n"
        "    [[ \"$actual_start\" == \"$expected_start\" ]] || die \"Start sector changed for $device\"\n"
        "    [[ \"$actual_size\" == \"$expected_size\" ]] || die \"Partition size changed for $device\"\n"
        "    [[ -n \"$expected_uuid\" && \"${actual_uuid,,}\" == \"${expected_uuid,,}\" ]] ||\n"
        "        die \"PARTUUID changed for $device\"\n"
        "    [[ -n \"$expected_type\" && \"${actual_type,,}\" == \"${expected_type,,}\" ]] ||\n"
        "        die \"GPT partition type changed for $device\"\n"
        "    ensure_node_idle \"$device\"\n"
        "}\n\n");
    writer_puts(writer,
        "verify_created_partition() {\n"
        "    local device=$1 expected_number=$2 usage=$3 planned_size=$4\n"
        "    local parent actual_number actual_type actual_uuid expected_type disk_pttype\n"
        "    local actual_start actual_start_bytes actual_size expected_mib expected_size\n"
        "    local minimum_size remaining_bytes previous_device='' previous_start previous_size\n"
        "    local previous_end gap candidate\n"
        "    [[ -b \"$device\" ]] || die \"Created partition is missing: $device\"\n"
        "    parent=$(lsblk -dnrpo PKNAME -- \"$device\")\n"
        "    actual_number=$(lsblk -dnro PARTN -- \"$device\")\n"
        "    actual_type=$(lsblk -dnro PARTTYPE -- \"$device\")\n"
        "    actual_uuid=$(lsblk -dnro PARTUUID -- \"$device\")\n"
        "    actual_start=$(lsblk -dnro START -- \"$device\")\n"
        "    actual_size=$(blockdev --getsize64 \"$device\")\n"
        "    disk_pttype=$(lsblk -dnro PTTYPE -- \"$TARGET_DISK\")\n"
        "    [[ \"$parent\" == \"$TARGET_DISK\" && \"$actual_number\" == \"$expected_number\" ]] ||\n"
        "        die \"Created partition identity mismatch for $device\"\n"
        "    [[ \"${disk_pttype,,}\" == gpt && -n \"$actual_uuid\" ]] ||\n"
        "        die \"Created partition has no stable GPT identity: $device\"\n"
        "    case \"$usage\" in\n"
        "        boot) expected_type='c12a7328-f81f-11d2-ba4b-00a0c93ec93b' ;;\n"
        "        swap) expected_type='0657fd6d-a4ab-43c4-84e5-0933c84b4f4f' ;;\n"
        "        *) expected_type='0fc63daf-8483-4772-8e79-3d69d8477de4' ;;\n"
        "    esac\n"
        "    [[ \"${actual_type,,}\" == \"$expected_type\" ]] || die \"Unexpected GPT type for $device\"\n"
        "    [[ \"$actual_start\" =~ ^[0-9]+$ ]] ||\n"
        "        die \"Invalid start geometry for $device\"\n"
        "    actual_start_bytes=$((actual_start * 512))\n"
        "    if [[ \"$expected_number\" -eq 1 ]]; then\n"
        "        (( actual_start_bytes > 0 && actual_start_bytes <= 64 * 1048576 )) ||\n"
        "            die \"Unexpected first-partition offset for $device\"\n"
        "    else\n"
        "        for candidate in \"${!PART_NUMBERS[@]}\"; do\n"
        "            if [[ \"${PART_NUMBERS[candidate]}\" -eq $((expected_number - 1)) ]]; then\n"
        "                previous_device=${PART_DEVICES[candidate]}\n"
        "                break\n"
        "            fi\n"
        "        done\n"
        "        [[ -n \"$previous_device\" && -b \"$previous_device\" ]] ||\n"
        "            die \"Cannot verify the partition preceding $device\"\n"
        "        previous_start=$(lsblk -dnro START -- \"$previous_device\")\n"
        "        previous_size=$(blockdev --getsize64 \"$previous_device\")\n"
        "        previous_end=$((previous_start * 512 + previous_size))\n"
        "        gap=$((actual_start_bytes - previous_end))\n"
        "        (( gap >= 0 && gap <= 64 * 1048576 )) ||\n"
        "            die \"Unexpected partition gap before $device\"\n"
        "    fi\n"
        "    case \"$usage\" in\n"
        "        boot) expected_mib=$AUTO_EFI_SIZE_MIB ;;\n"
        "        root) [[ \"$STORAGE_MODE\" == auto-root-only ]] && expected_mib=0 || expected_mib=$AUTO_ROOT_SIZE_MIB ;;\n"
        "        home) expected_mib=$AUTO_HOME_SIZE_MIB ;;\n"
        "        swap) expected_mib=$AUTO_SWAP_SIZE_MIB ;;\n"
        "    esac\n"
        "    if (( expected_mib > 0 )); then\n"
        "        expected_size=$((expected_mib * 1048576))\n"
        "        [[ \"$actual_size\" == \"$expected_size\" ]] || die \"Unexpected size for $device\"\n"
        "    else\n"
        "        minimum_size=$((planned_size - 64 * 1048576))\n"
        "        remaining_bytes=$((EXPECTED_SIZE - actual_start_bytes - actual_size))\n"
        "        (( actual_size >= minimum_size && actual_size <= planned_size &&\n"
        "           remaining_bytes >= 0 && remaining_bytes <= 64 * 1048576 )) ||\n"
        "            die \"Unexpected fill-to-end size for $device\"\n"
        "    fi\n"
        "    ensure_node_idle \"$device\"\n"
        "}\n\n");
    writer_puts(writer,
        "verify_storage_state() {\n"
        "    local index node mounted_target action filesystem actual actual_uuid mount_targets disk_nodes\n"
        "    [[ -d \"$TARGET_ROOT\" && ! -L \"$TARGET_ROOT\" ]] ||\n"
        "        die \"Target mountpoint changed or became a symlink: $TARGET_ROOT\"\n"
        "    verify_disk_identity\n"
        "    mount_targets=$(findmnt -rn -o TARGET) || die 'Cannot inspect active mounts.'\n"
        "    while IFS= read -r mounted_target; do\n"
        "        case \"$mounted_target\" in\n"
        "            \"$TARGET_ROOT\"|\"$TARGET_ROOT\"/*) die \"$mounted_target is mounted below $TARGET_ROOT\" ;;\n"
        "        esac\n"
        "    done <<<\"$mount_targets\"\n"
        "    if [[ \"$STORAGE_MODE\" == existing ]]; then\n"
        "        ensure_node_idle \"$TARGET_DISK\"\n"
        "        for ((index=0; index<${#PART_DEVICES[@]}; ++index)); do\n"
        "            verify_existing_partition \"${PART_DEVICES[index]}\" \"${PART_NUMBERS[index]}\" \\\n"
        "                \"${PART_UUIDS[index]}\" \"${PART_START_SECTORS[index]}\" \\\n"
        "                \"${PART_SIZES[index]}\" \"${PART_TYPES[index]}\"\n"
        "        done\n"
        "    else\n"
        "        disk_nodes=$(lsblk -nrpo NAME -- \"$TARGET_DISK\") ||\n"
        "            die 'Cannot enumerate target-disk nodes.'\n"
        "        while IFS= read -r node; do\n"
        "            [[ -n \"$node\" ]] && ensure_node_idle \"$node\"\n"
        "        done <<<\"$disk_nodes\"\n"
        "    fi\n"
        "    for ((index=0; index<${#PART_DEVICES[@]}; ++index)); do\n"
        "        action=${PART_ACTIONS[index]}\n"
        "        filesystem=${PART_FILESYSTEMS[index]}\n"
        "        [[ \"$action\" == keep ]] || continue\n"
        "        actual=$(blkid -s TYPE -o value -- \"${PART_DEVICES[index]}\") ||\n"
        "            die \"Cannot identify the kept filesystem on ${PART_DEVICES[index]}\"\n"
        "        actual=$(normalize_fs \"$actual\")\n"
        "        [[ \"$actual\" == \"$filesystem\" ]] ||\n"
        "            die \"Kept filesystem changed on ${PART_DEVICES[index]} (expected $filesystem, got ${actual:-none})\"\n"
        "        actual_uuid=$(blkid -s UUID -o value -- \"${PART_DEVICES[index]}\") ||\n"
        "            die \"Cannot identify the kept filesystem UUID on ${PART_DEVICES[index]}\"\n"
        "        [[ -n \"${PART_FS_UUIDS[index]}\" &&\n"
        "           \"${actual_uuid,,}\" == \"${PART_FS_UUIDS[index],,}\" ]] ||\n"
        "            die \"Filesystem UUID changed on ${PART_DEVICES[index]}\"\n"
        "    done\n"
        "}\n\n");
    writer_puts(writer,
        "probe_kept_filesystems() {\n"
        "    local index device filesystem options mounted_source has_keep=false\n"
        "    for ((index=0; index<${#PART_ACTIONS[@]}; ++index)); do\n"
        "        [[ \"${PART_ACTIONS[index]}\" != keep ]] || has_keep=true\n"
        "    done\n"
        "    [[ \"$has_keep\" == true ]] || return 0\n"
        "    KEEP_PROBE_MOUNT=$WORK_DIR/keep-probe\n"
        "    install -d -m 0700 \"$KEEP_PROBE_MOUNT\"\n"
        "    phase 'Read-only probing of filesystems marked KEEP'\n"
        "    for ((index=0; index<${#PART_DEVICES[@]}; ++index)); do\n"
        "        [[ \"${PART_ACTIONS[index]}\" == keep ]] || continue\n"
        "        device=${PART_DEVICES[index]}\n"
        "        filesystem=${PART_FILESYSTEMS[index]}\n"
        "        if [[ \"$filesystem\" == swap ]]; then\n"
        "            require_command swaplabel\n"
        "            swaplabel \"$device\" >/dev/null ||\n"
        "                die \"The kept swap header cannot be read: $device\"\n"
        "            continue\n"
        "        fi\n"
        "        case \"$filesystem\" in\n"
        "            vfat) options='ro,nodev,nosuid,noexec' ;;\n"
        "            ext4) options='ro,noload,nodev,nosuid,noexec' ;;\n"
        "            xfs) options='ro,norecovery,nodev,nosuid,noexec' ;;\n"
        "            f2fs) options='ro,disable_roll_forward,nodev,nosuid,noexec' ;;\n"
        "            *) die \"Cannot probe unsupported kept filesystem: $filesystem\" ;;\n"
        "        esac\n"
        "        KEEP_PROBE_SOURCE=$device\n"
        "        KEEP_PROBE_ACTIVE=true\n"
        "        if ! mount -t \"$filesystem\" -o \"$options\" -- \"$device\" \"$KEEP_PROBE_MOUNT\"; then\n"
        "            die \"The kept filesystem cannot be mounted read-only: $device\"\n"
        "        fi\n"
        "        mounted_source=$(findmnt -rn --mountpoint \"$KEEP_PROBE_MOUNT\" -o SOURCE) ||\n"
        "            die \"Cannot verify the KEEP probe mount for $device\"\n"
        "        [[ \"$mounted_source\" == \"$device\" ]] ||\n"
        "            die \"The KEEP probe mounted an unexpected source for $device\"\n"
        "        umount -- \"$KEEP_PROBE_MOUNT\" || die \"Cannot unmount the KEEP probe for $device\"\n"
        "        KEEP_PROBE_ACTIVE=false\n"
        "        KEEP_PROBE_SOURCE=''\n"
        "    done\n"
        "    rmdir -- \"$KEEP_PROBE_MOUNT\" || die 'Cannot remove the KEEP probe directory.'\n"
        "    KEEP_PROBE_MOUNT=''\n"
        "}\n\n");
    writer_puts(writer,
        "preflight() {\n"
        "    local command filesystem index efi_type efi_options\n"
        "    [[ \"$EUID\" -eq 0 ]] || die 'Run this installer as root.'\n"
        "    [[ -d /sys/firmware/efi ]] || die 'The live environment was not booted in UEFI mode.'\n"
        "    for command in bash tee sleep lsblk blockdev sed grep find findmnt wipefs sfdisk blkid mount umount swapon swapoff pacman pacstrap genfstab arch-chroot mktemp mkdir rmdir install cp rm chmod mv sync; do\n"
        "        require_command \"$command\"\n"
        "    done\n"
        "    [[ ! -L \"$TARGET_ROOT\" ]] || die \"Target mountpoint must not be a symlink: $TARGET_ROOT\"\n"
        "    mkdir -p -- \"$TARGET_ROOT\"\n"
        "    [[ -d \"$TARGET_ROOT\" ]] || die \"Target mountpoint is not a directory: $TARGET_ROOT\"\n"
        "    [[ -f \"/usr/share/zoneinfo/$TARGET_TIMEZONE\" ]] || die \"Timezone data is unavailable: $TARGET_TIMEZONE\"\n"
        "    [[ \"${#PART_DEVICES[@]}\" -eq \"${#PART_USAGES[@]}\" &&\n"
        "       \"${#PART_DEVICES[@]}\" -eq \"${#PART_ACTIONS[@]}\" &&\n"
        "       \"${#PART_DEVICES[@]}\" -eq \"${#PART_FILESYSTEMS[@]}\" &&\n"
        "       \"${#PART_DEVICES[@]}\" -eq \"${#PART_F2FS_MODES[@]}\" &&\n"
        "       \"${#PART_DEVICES[@]}\" -eq \"${#PART_MOUNTPOINTS[@]}\" &&\n"
        "       \"${#PART_DEVICES[@]}\" -eq \"${#PART_FS_UUIDS[@]}\" &&\n"
        "       \"${#PART_DEVICES[@]}\" -eq \"${#PART_UUIDS[@]}\" &&\n"
        "       \"${#PART_DEVICES[@]}\" -eq \"${#PART_TYPES[@]}\" &&\n"
        "       \"${#PART_DEVICES[@]}\" -eq \"${#PART_NUMBERS[@]}\" &&\n"
        "       \"${#PART_DEVICES[@]}\" -eq \"${#PART_START_SECTORS[@]}\" &&\n"
        "       \"${#PART_DEVICES[@]}\" -eq \"${#PART_SIZES[@]}\" ]] || die 'Partition plan arrays are inconsistent.'\n"
        "    verify_storage_state\n"
        "    for ((index=0; index<${#PART_DEVICES[@]}; ++index)); do\n"
        "        [[ \"${PART_ACTIONS[index]}\" != keep ]] || continue\n"
        "        filesystem=${PART_FILESYSTEMS[index]}\n"
        "        case \"$filesystem\" in\n"
        "            vfat) require_command mkfs.fat ;;\n"
        "            ext4) require_command mkfs.ext4 ;;\n"
        "            xfs) require_command mkfs.xfs ;;\n"
        "            f2fs) require_command mkfs.f2fs ;;\n"
        "            swap) require_command mkswap ;;\n"
        "            *) die \"Unsupported filesystem: $filesystem\" ;;\n"
        "        esac\n"
        "    done\n"
        "    if [[ \"$CREATE_EFI_ENTRY\" == true ]]; then\n"
        "        [[ -d /sys/firmware/efi/efivars ]] || die 'EFI variables are unavailable.'\n"
        "        efi_type=$(findmnt -rn -T /sys/firmware/efi/efivars -o FSTYPE)\n"
        "        efi_options=$(findmnt -rn -T /sys/firmware/efi/efivars -o OPTIONS)\n"
        "        [[ \"$efi_type\" == efivarfs && \",$efi_options,\" != *,ro,* ]] ||\n"
        "            die 'EFI variables are not mounted read-write.'\n"
        "    fi\n"
        "    [[ \"$USE_LOCAL_MIRROR\" != true ]] || select_local_mirror_source\n"
        "    verify_secure_boot_assets\n"
        "}\n\n");
    writer_puts(writer,
        "print_plan() {\n"
        "    local index\n"
        "    printf 'Target: %s  (%s, %s bytes)\\n' \"$TARGET_DISK\" \"${EXPECTED_MODEL:-unknown model}\" \"$EXPECTED_SIZE\"\n"
        "    printf 'Storage mode: %s\\n' \"$STORAGE_MODE\"\n"
        "    if [[ \"$USE_LOCAL_MIRROR\" == true ]]; then\n"
        "        printf 'Local mirror: %s  UUID=%s  parent=%s  serial=%s\\n' \\\n"
        "            \"$LOCAL_MIRROR_SOURCE\" \"$LOCAL_MIRROR_UUID\" \\\n"
        "            \"$LOCAL_MIRROR_PARENT\" \"${LOCAL_MIRROR_PARENT_SERIAL:-unknown}\"\n"
        "    fi\n"
        "    printf '%-22s %-8s %-8s %-7s %s\\n' DEVICE ACTION FS USAGE MOUNTPOINT\n"
        "    for ((index=0; index<${#PART_DEVICES[@]}; ++index)); do\n"
        "        printf '%-22s %-8s %-8s %-7s %s\\n' \\\n"
        "            \"${PART_DEVICES[index]}\" \"${PART_ACTIONS[index]}\" \\\n"
        "            \"${PART_FILESYSTEMS[index]}\" \"${PART_USAGES[index]}\" \\\n"
        "            \"${PART_MOUNTPOINTS[index]}\"\n"
        "    done\n"
        "}\n\n"
        "confirm_package_preparation() {\n"
        "    local answer\n"
        "    [[ -t 0 ]] || die 'Package preparation requires an interactive terminal.'\n"
        "    printf '\\nPackage preparation refreshes the Live package databases.\\n'\n"
        "    if [[ \"$USE_LOCAL_MIRROR\" == true ]]; then\n"
        "        printf 'WARNING: the selected local repository disables package signature verification.\\n'\n"
        "        printf 'It will mount %s read-only and temporarily edit Live pacman configuration.\\n' \\\n"
        "            \"$LOCAL_MIRROR_SOURCE\"\n"
        "        printf 'Type UNSIGNED %s %s to trust this exact source: ' \\\n"
        "            \"$LOCAL_MIRROR_SOURCE\" \"$LOCAL_MIRROR_UUID\"\n"
        "        IFS= read -r answer || die 'Preparation confirmation was interrupted.'\n"
        "        [[ \"$answer\" == \"UNSIGNED $LOCAL_MIRROR_SOURCE $LOCAL_MIRROR_UUID\" ]] ||\n"
        "            die 'Unsigned local-mirror preparation was not confirmed.'\n"
        "        verify_local_mirror_identity\n"
        "        return 0\n"
        "    fi\n"
        "    printf 'Type PREPARE to continue: '\n"
        "    IFS= read -r answer || die 'Preparation confirmation was interrupted.'\n"
        "    [[ \"$answer\" == PREPARE ]] || die 'Package preparation was not confirmed.'\n"
        "}\n\n"
        "confirm_destructive_actions() {\n"
        "    local answer\n"
        "    [[ -t 0 ]] || die 'Destructive confirmation requires an interactive terminal.'\n"
        "    printf '\\nThis plan can overwrite filesystems and cannot be rolled back.\\n'\n"
        "    printf 'Type the full target disk path (%s) to continue: ' \"$TARGET_DISK\"\n"
        "    IFS= read -r answer || die 'Confirmation input was interrupted.'\n"
        "    [[ \"$answer\" == \"$TARGET_DISK\" ]] || die 'Target disk confirmation did not match.'\n"
        "}\n\n");
    writer_puts(writer,
        "partition_disk() {\n"
        "    [[ \"$STORAGE_MODE\" != existing ]] || return 0\n"
        "    case \"$STORAGE_MODE\" in\n"
        "        auto-root-swap)\n"
        "            (( AUTO_EFI_SIZE_MIB > 0 && AUTO_ROOT_SIZE_MIB > 0 && AUTO_SWAP_SIZE_MIB > 0 )) ||\n"
        "                die 'Automatic partition sizes are invalid.'\n"
        "            ;;\n"
        "        auto-home-swap)\n"
        "            (( AUTO_EFI_SIZE_MIB > 0 && AUTO_ROOT_SIZE_MIB > 0 && AUTO_HOME_SIZE_MIB > 0 && AUTO_SWAP_SIZE_MIB > 0 )) ||\n"
        "                die 'Automatic partition sizes are invalid.'\n"
        "            ;;\n"
        "        auto-root-only)\n"
        "            (( AUTO_EFI_SIZE_MIB > 0 && EXPECTED_SIZE > (AUTO_EFI_SIZE_MIB + 8192) * 1048576 )) ||\n"
        "                die 'Automatic root-only sizes are invalid.'\n"
        "            ;;\n"
        "        *) die \"Unknown storage mode: $STORAGE_MODE\" ;;\n"
        "    esac\n"
        "    phase 'Rebuilding the GPT partition table'\n"
        "    wipefs --all --force \"$TARGET_DISK\"\n"
        "    case \"$STORAGE_MODE\" in\n"
        "        auto-root-swap)\n"
        "            sfdisk --wipe always --wipe-partitions always \"$TARGET_DISK\" <<SFDISK\n"
        "label: gpt\n"
        "size=${AUTO_EFI_SIZE_MIB}MiB,type=uefi,name=\"EFI System\"\n"
        "size=${AUTO_ROOT_SIZE_MIB}MiB,type=linux,name=\"Arch Linux root\"\n"
        "size=${AUTO_SWAP_SIZE_MIB}MiB,type=swap,name=\"Linux swap\"\n"
        "SFDISK\n"
        "            ;;\n"
        "        auto-home-swap)\n"
        "            sfdisk --wipe always --wipe-partitions always \"$TARGET_DISK\" <<SFDISK\n"
        "label: gpt\n"
        "size=${AUTO_EFI_SIZE_MIB}MiB,type=uefi,name=\"EFI System\"\n"
        "size=${AUTO_ROOT_SIZE_MIB}MiB,type=linux,name=\"Arch Linux root\"\n"
        "size=${AUTO_HOME_SIZE_MIB}MiB,type=linux,name=\"Arch Linux home\"\n"
        "size=${AUTO_SWAP_SIZE_MIB}MiB,type=swap,name=\"Linux swap\"\n"
        "SFDISK\n"
        "            ;;\n"
        "        auto-root-only)\n"
        "            sfdisk --wipe always --wipe-partitions always \"$TARGET_DISK\" <<SFDISK\n"
        "label: gpt\n"
        "size=${AUTO_EFI_SIZE_MIB}MiB,type=uefi,name=\"EFI System\"\n"
        "size=,type=linux,name=\"Arch Linux root\"\n"
        "SFDISK\n"
        "            ;;\n"
        "        *) die \"Unknown storage mode: $STORAGE_MODE\" ;;\n"
        "    esac\n"
        "    if command -v partprobe >/dev/null 2>&1; then\n"
        "        partprobe \"$TARGET_DISK\"\n"
        "    else\n"
        "        blockdev --rereadpt \"$TARGET_DISK\"\n"
        "    fi\n"
        "    if command -v udevadm >/dev/null 2>&1; then udevadm settle; fi\n"
        "}\n\n"
        "wait_for_partitions() {\n"
        "    local index device attempt\n"
        "    for ((index=0; index<${#PART_DEVICES[@]}; ++index)); do\n"
        "        device=${PART_DEVICES[index]}\n"
        "        for ((attempt=0; attempt<50; ++attempt)); do\n"
        "            [[ -b \"$device\" ]] && break\n"
        "            sleep 0.1\n"
        "        done\n"
        "        [[ -b \"$device\" ]] || die \"Partition did not appear: $device\"\n"
        "        if [[ \"$STORAGE_MODE\" != existing ]]; then\n"
        "            verify_created_partition \"$device\" \"${PART_NUMBERS[index]}\" \\\n"
        "                \"${PART_USAGES[index]}\" \"${PART_SIZES[index]}\"\n"
        "        fi\n"
        "    done\n"
        "}\n\n");
    writer_puts(writer,
        "format_partitions() {\n"
        "    local index device action filesystem actual actual_uuid\n"
        "    phase 'Applying filesystem actions'\n"
        "    for ((index=0; index<${#PART_DEVICES[@]}; ++index)); do\n"
        "        device=${PART_DEVICES[index]}\n"
        "        action=${PART_ACTIONS[index]}\n"
        "        filesystem=${PART_FILESYSTEMS[index]}\n"
        "        if [[ \"$action\" == keep ]]; then\n"
        "            actual=$(blkid -s TYPE -o value -- \"$device\") ||\n"
        "                die \"Cannot identify the kept filesystem on $device\"\n"
        "            actual=$(normalize_fs \"$actual\")\n"
        "            [[ \"$actual\" == \"$filesystem\" ]] ||\n"
        "                die \"Kept filesystem changed on $device (expected $filesystem, got ${actual:-none})\"\n"
        "            actual_uuid=$(blkid -s UUID -o value -- \"$device\") ||\n"
        "                die \"Cannot identify the kept filesystem UUID on $device\"\n"
        "            [[ -n \"${PART_FS_UUIDS[index]}\" &&\n"
        "               \"${actual_uuid,,}\" == \"${PART_FS_UUIDS[index],,}\" ]] ||\n"
        "                die \"Filesystem UUID changed on $device\"\n"
        "            continue\n"
        "        fi\n"
        "        printf 'Formatting %s as %s\\n' \"$device\" \"$filesystem\"\n"
        "        case \"$filesystem\" in\n"
        "            vfat) mkfs.fat -F 32 \"$device\" ;;\n"
        "            ext4) mkfs.ext4 -F \"$device\" ;;\n"
        "            xfs) mkfs.xfs -f \"$device\" ;;\n"
        "            f2fs) mkfs.f2fs -f -O extra_attr,inode_checksum,sb_checksum,compression \"$device\" ;;\n"
        "            swap) mkswap --force \"$device\" ;;\n"
        "            *) die \"Unsupported filesystem: $filesystem\" ;;\n"
        "        esac\n"
        "    done\n"
        "}\n\n");
    writer_puts(writer,
        "mount_filesystems() {\n"
        "    local index device usage filesystem mode mountpoint destination options\n"
        "    phase 'Mounting filesystems in path order'\n"
        "    for ((index=0; index<${#PART_DEVICES[@]}; ++index)); do\n"
        "        device=${PART_DEVICES[index]}\n"
        "        usage=${PART_USAGES[index]}\n"
        "        [[ \"$usage\" != swap ]] || continue\n"
        "        filesystem=${PART_FILESYSTEMS[index]}\n"
        "        mode=${PART_F2FS_MODES[index]}\n"
        "        mountpoint=${PART_MOUNTPOINTS[index]}\n"
        "        if [[ \"$mountpoint\" == / ]]; then\n"
        "            destination=$TARGET_ROOT\n"
        "            TARGET_MOUNTED=true\n"
        "        else\n"
        "            destination=$TARGET_ROOT$mountpoint\n"
        "            [[ ! -L \"$destination\" ]] || die \"Mountpoint must not be a symlink: $destination\"\n"
        "            mkdir -p -- \"$destination\"\n"
        "        fi\n"
        "        options=''\n"
        "        if [[ \"$filesystem\" == f2fs ]]; then\n"
        "            case \"$mode\" in\n"
        "                balanced) options='noatime,lazytime,gc_merge,atgc,nodiscard,fsync_mode=nobarrier' ;;\n"
        "                compressed) options='noatime,lazytime,gc_merge,atgc,nodiscard,fsync_mode=nobarrier,compress_algorithm=zstd:6,compress_chksum' ;;\n"
        "            esac\n"
        "        fi\n"
        "        if [[ -n \"$options\" ]]; then\n"
        "            mount -o \"$options\" -- \"$device\" \"$destination\"\n"
        "        else\n"
        "            mount -- \"$device\" \"$destination\"\n"
        "        fi\n"
        "    done\n"
        "    [[ \"$TARGET_MOUNTED\" == true ]] || die 'The root filesystem was not mounted.'\n"
        "    for ((index=0; index<${#PART_DEVICES[@]}; ++index)); do\n"
        "        [[ \"${PART_USAGES[index]}\" == swap ]] || continue\n"
        "        SWAPS_TO_DISABLE+=(\"${PART_DEVICES[index]}\")\n"
        "        swapon -- \"${PART_DEVICES[index]}\"\n"
        "    done\n"
        "}\n\n"
        "setup_local_mirror() {\n"
        "    local mount_status path\n"
        "    [[ \"$USE_LOCAL_MIRROR\" == true ]] || return 0\n"
        "    phase 'Mounting the confirmed local package mirror'\n"
        "    verify_local_mirror_identity\n"
        "    for path in /run /run/media /run/media/root /run/media/root/F2FS-DATA; do\n"
        "        [[ ! -L \"$path\" ]] || die \"Local mirror path component is a symlink: $path\"\n"
        "    done\n"
        "    if findmnt -rn --mountpoint /run/media/root/F2FS-DATA >/dev/null 2>&1; then\n"
        "        die '/run/media/root/F2FS-DATA is already a mountpoint.'\n"
        "    else\n"
        "        mount_status=$?\n"
        "        [[ \"$mount_status\" -eq 1 ]] || die 'Cannot inspect the local mirror mountpoint.'\n"
        "    fi\n"
        "    mkdir -p -- /run/media/root/F2FS-DATA\n"
        "    for path in /run /run/media /run/media/root /run/media/root/F2FS-DATA; do\n"
        "        [[ ! -L \"$path\" ]] || die \"Local mirror path component became a symlink: $path\"\n"
        "    done\n"
        "    LOCAL_MIRROR_MOUNTED=true\n"
        "    mount -o ro,nodev,nosuid,noexec -- \"$LOCAL_MIRROR_SOURCE\" /run/media/root/F2FS-DATA\n"
        "    [[ \"$(findmnt -rn --mountpoint /run/media/root/F2FS-DATA -o SOURCE)\" == \\\n"
        "       \"$LOCAL_MIRROR_SOURCE\" ]] || die 'The local mirror mounted from an unexpected source.'\n"
        "    [[ -d /run/media/root/F2FS-DATA/repo/archlinux ]] ||\n"
        "        die 'The F2FS-DATA partition does not contain repo/archlinux.'\n"
        "    cp -a /etc/pacman.conf \"$WORK_DIR/host-pacman.conf\"\n"
        "    cp -a /etc/pacman.d/mirrorlist \"$WORK_DIR/host-mirrorlist\"\n"
        "    HOST_PACMAN_CHANGED=true\n"
        "    sed -i -E 's/^[[:space:]#]*SigLevel[[:space:]]*=.*/SigLevel = Never/' /etc/pacman.conf\n"
        "    printf '%s\\n' 'Server = file:///run/media/root/F2FS-DATA/repo/archlinux/$repo/os/$arch' > /etc/pacman.d/mirrorlist\n"
        "    pacman -Syy --noconfirm\n"
        "}\n\n");
    writer_puts(writer,
        "setup_target_local_mirror() {\n"
        "    local mount_status path source uuid filesystem options option\n"
        "    [[ \"$USE_LOCAL_MIRROR\" == true ]] || return 0\n"
        "    for path in \"$TARGET_ROOT/var\" \"$TARGET_ROOT/var/cache\" \"$TARGET_LOCAL_MIRROR\"; do\n"
        "        [[ ! -L \"$path\" ]] || die \"Target local-mirror path is a symlink: $path\"\n"
        "    done\n"
        "    mkdir -p -- \"$TARGET_LOCAL_MIRROR\"\n"
        "    if findmnt -rn --mountpoint \"$TARGET_LOCAL_MIRROR\" >/dev/null 2>&1; then\n"
        "        die \"$TARGET_LOCAL_MIRROR is already a mountpoint.\"\n"
        "    else\n"
        "        mount_status=$?\n"
        "        [[ \"$mount_status\" -eq 1 ]] || die 'Cannot inspect the target local-mirror mountpoint.'\n"
        "    fi\n"
        "    TARGET_LOCAL_MIRROR_MOUNTED=true\n"
        "    mount --bind -- /run/media/root/F2FS-DATA/repo/archlinux \"$TARGET_LOCAL_MIRROR\"\n"
        "    mount -o remount,bind,ro,nodev,nosuid,noexec -- \"$TARGET_LOCAL_MIRROR\"\n"
        "    source=$(findmnt -rn --mountpoint \"$TARGET_LOCAL_MIRROR\" -o SOURCE) ||\n"
        "        die 'Cannot identify the target local-mirror source.'\n"
        "    uuid=$(findmnt -rn --mountpoint \"$TARGET_LOCAL_MIRROR\" -o UUID) ||\n"
        "        die 'Cannot identify the target local-mirror UUID.'\n"
        "    filesystem=$(findmnt -rn --mountpoint \"$TARGET_LOCAL_MIRROR\" -o FSTYPE) ||\n"
        "        die 'Cannot identify the target local-mirror filesystem.'\n"
        "    options=$(findmnt -rn --mountpoint \"$TARGET_LOCAL_MIRROR\" -o OPTIONS) ||\n"
        "        die 'Cannot identify the target local-mirror options.'\n"
        "    [[ \"$source\" == \"$LOCAL_MIRROR_SOURCE\" ||\n"
        "       \"$source\" == \"$LOCAL_MIRROR_SOURCE[\"* ]] ||\n"
        "        die 'The target local mirror has an unexpected source.'\n"
        "    [[ \"${uuid,,}\" == \"${LOCAL_MIRROR_UUID,,}\" && \"${filesystem,,}\" == f2fs ]] ||\n"
        "        die 'The target local-mirror identity is incorrect.'\n"
        "    for option in ro nodev nosuid noexec; do\n"
        "        [[ \",$options,\" == *,\"$option\",* ]] ||\n"
        "            die \"The target local mirror is missing mount option: $option\"\n"
        "    done\n"
        "}\n\n"
        "unmount_target_local_mirror() {\n"
        "    local uuid filesystem\n"
        "    [[ \"$TARGET_LOCAL_MIRROR_MOUNTED\" == true ]] || return 0\n"
        "    uuid=$(findmnt -rn --mountpoint \"$TARGET_LOCAL_MIRROR\" -o UUID) ||\n"
        "        die 'Cannot recheck the target local-mirror UUID before unmounting.'\n"
        "    filesystem=$(findmnt -rn --mountpoint \"$TARGET_LOCAL_MIRROR\" -o FSTYPE) ||\n"
        "        die 'Cannot recheck the target local-mirror filesystem before unmounting.'\n"
        "    [[ \"${uuid,,}\" == \"${LOCAL_MIRROR_UUID,,}\" && \"${filesystem,,}\" == f2fs ]] ||\n"
        "        die 'Refusing to unmount an unexpected target local mirror.'\n"
        "    umount -- \"$TARGET_LOCAL_MIRROR\"\n"
        "    TARGET_LOCAL_MIRROR_MOUNTED=false\n"
        "    rmdir -- \"$TARGET_LOCAL_MIRROR\"\n"
        "}\n\n");
    writer_puts(writer,
        "prepare_package_source() {\n"
        "    if [[ \"$USE_LOCAL_MIRROR\" == true ]]; then\n"
        "        setup_local_mirror\n"
        "    else\n"
        "        phase 'Checking package repositories'\n"
        "        pacman -Syy --noconfirm\n"
        "    fi\n"
        "    if [[ \"$ENABLE_SECURE_BOOT\" == true ]]; then\n"
        "        phase 'Installing the Live signing tool'\n"
        "        pacman -S --needed --noconfirm sbsigntools\n"
        "        require_command sbsign\n"
        "        require_command sbverify\n"
        "        require_command bsdtar\n"
        "    fi\n"
        "    phase 'Resolving the complete package selection'\n"
        "    pacman -Sp --needed --noconfirm \"${REQUIRED_PACKAGES[@]}\" >/dev/null ||\n"
        "        die 'One or more selected packages cannot be resolved before installation.'\n"
        "}\n\n"
        "install_base_system() {\n"
        "    local index uuid packages=(\n"
        "        base base-devel linux-firmware dosfstools xfsprogs f2fs-tools\n"
        "        exfatprogs btrfs-progs ntfsprogs nano vi man-db man-pages texinfo\n"
        "        \"$KERNEL_PACKAGE\" \"$KERNEL_PACKAGE-headers\"\n"
        "    )\n"
        "    [[ -z \"$MICROCODE_PACKAGE\" ]] || packages+=(\"$MICROCODE_PACKAGE\")\n"
        "    [[ \"$IS_LAPTOP\" != true ]] || packages+=(sof-firmware)\n"
        "    phase 'Installing the base system'\n"
        "    pacstrap -K \"$TARGET_ROOT\" \"${packages[@]}\"\n"
        "    genfstab -U -f \"$TARGET_ROOT\" \"$TARGET_ROOT\" > \"$TARGET_ROOT/etc/fstab\"\n"
        "    for ((index=0; index<${#PART_DEVICES[@]}; ++index)); do\n"
        "        [[ \"${PART_USAGES[index]}\" == swap ]] || continue\n"
        "        uuid=$(blkid -s UUID -o value -- \"${PART_DEVICES[index]}\")\n"
        "        [[ -n \"$uuid\" ]] || die \"Cannot determine swap UUID for ${PART_DEVICES[index]}\"\n"
        "        printf 'UUID=%s none swap defaults 0 0\\n' \"$uuid\" >> \"$TARGET_ROOT/etc/fstab\"\n"
        "    done\n"
        "    if [[ \"$USE_LOCAL_MIRROR\" == true ]]; then\n"
        "        cp -a \"$TARGET_ROOT/etc/pacman.conf\" \"$WORK_DIR/target-pacman.conf\"\n"
        "        cp -a \"$TARGET_ROOT/etc/pacman.d/mirrorlist\" \"$WORK_DIR/target-mirrorlist\"\n"
        "        setup_target_local_mirror\n"
        "        sed -i -E 's/^[[:space:]#]*SigLevel[[:space:]]*=.*/SigLevel = Never/' \"$TARGET_ROOT/etc/pacman.conf\"\n"
        "        printf '%s\\n' 'Server = file:///var/cache/arch-install-repo/$repo/os/$arch' > \"$TARGET_ROOT/etc/pacman.d/mirrorlist\"\n"
        "    fi\n"
        "}\n\n");
}

static bool emit_chroot_configuration(ScriptWriter *writer, const InstallPlan *plan)
{
    const char *desktop;

    switch (plan->system.desktop) {
    case DESKTOP_KDE:
        desktop = "kde";
        break;
    case DESKTOP_GNOME:
        desktop = "gnome";
        break;
    case DESKTOP_HYPRLAND:
        desktop = "hyprland";
        break;
    case DESKTOP_NONE:
        desktop = "none";
        break;
    default:
        desktop = "none";
        break;
    }

    writer_puts(writer,
                "write_chroot_script() {\n"
                "    cat > \"$TARGET_ROOT/root/.arch-install-chroot.sh\" <<'ARCH_CHROOT_SCRIPT'\n"
                "#!/usr/bin/bash\n"
                "set -Eeuo pipefail\n"
                "PATH='/usr/bin'\n"
                "export PATH\n"
                "readonly PATH\n"
                "umask 022\n\n");
    if (!emit_assignment(writer, "TIMEZONE", plan->system.timezone) ||
        !emit_assignment(writer, "LOCALE", locale_name(plan->system.locale)) ||
        !emit_assignment(writer, "HOSTNAME_VALUE", plan->system.hostname) ||
        !emit_assignment(writer, "USERNAME", plan->system.username) ||
        !emit_assignment(writer, "ROOT_DEVICE", find_partition(plan, PART_ROOT)->device) ||
        !emit_assignment(writer, "KERNEL_IMAGE", kernel_name(plan->system.kernel)) ||
        !emit_assignment(writer, "MICROCODE_PACKAGE",
                         plan->system.platform == PLATFORM_INTEL
                             ? "intel-ucode"
                             : plan->system.platform == PLATFORM_AMD ? "amd-ucode" : "") ||
        !emit_assignment(writer, "DESKTOP", desktop)) {
        return false;
    }
    emit_boolean(writer, "IS_LAPTOP", plan->system.laptop);
    emit_boolean(writer, "INTEL_GRAPHICS", plan->system.intel_graphics);
    emit_boolean(writer, "NVIDIA_GRAPHICS", plan->system.nvidia_graphics);
    emit_boolean(writer, "HAS_BLUETOOTH", plan->system.bluetooth);
    emit_boolean(writer, "DESKTOP_RECOMMENDED", plan->system.desktop_recommended);
    emit_boolean(writer, "CHINESE_INPUT", plan->system.chinese_input);
    emit_boolean(writer, "ENABLE_FIREWALL", plan->system.firewall);
    emit_boolean(writer, "ENABLE_PRINTER", plan->system.printer);
    emit_boolean(writer, "INSTALL_ARCHIVE_TOOLS", plan->system.archive_tools);
    emit_boolean(writer, "INSTALL_TERMINAL_TOOLS", plan->system.terminal_tools);
    emit_boolean(writer, "INSTALL_EXTRA_TOOLS", plan->system.extra_tools);
    emit_boolean(writer, "INSTALL_DESKTOP_APPS", plan->system.desktop_apps);
    emit_boolean(writer, "USE_CHINA_MIRRORS", plan->system.china_mirrors);
    emit_boolean(writer, "ENABLE_SECURE_BOOT", plan->system.secure_boot);
    writer_puts(writer,
        "ROOT_UUID=$(blkid -s UUID -o value -- \"$ROOT_DEVICE\") || {\n"
        "    printf 'Cannot determine the root filesystem UUID.\\n' >&2\n"
        "    exit 1\n"
        "}\n"
        "[[ -n \"$ROOT_UUID\" ]] || { printf 'The root filesystem UUID is empty.\\n' >&2; exit 1; }\n"
        "readonly ROOT_UUID\n"
        "readonly KERNEL_FILE=\"vmlinuz-$KERNEL_IMAGE\"\n"
        "readonly INITRAMFS_FILE=\"initramfs-$KERNEL_IMAGE.img\"\n"
        "readonly FALLBACK_FILE=\"initramfs-$KERNEL_IMAGE-fallback.img\"\n"
        "readonly MICROCODE_FILE=\"${MICROCODE_PACKAGE:+$MICROCODE_PACKAGE.img}\"\n\n"
        "pacman_install() {\n"
        "    pacman -S --needed --noconfirm \"$@\"\n"
        "}\n\n"
        "set_account_password() {\n"
        "    local account=$1 attempt\n"
        "    for attempt in 1 2 3; do\n"
        "        if passwd \"$account\"; then return 0; fi\n"
        "        printf 'Password update failed for %s (attempt %d of 3).\\n' \\\n"
        "            \"$account\" \"$attempt\" >&2\n"
        "    done\n"
        "    printf 'Giving up after three failed password attempts for %s.\\n' \"$account\" >&2\n"
        "    return 1\n"
        "}\n\n"
        "configure_base() {\n"
        "    printf '\\n==> Configuring locale, clock, and host identity\\n'\n"
        "    [[ -f \"/usr/share/zoneinfo/$TIMEZONE\" ]] || { printf 'Invalid timezone: %s\\n' \"$TIMEZONE\" >&2; exit 1; }\n"
        "    ln -sf -- \"/usr/share/zoneinfo/$TIMEZONE\" /etc/localtime\n"
        "    hwclock --systohc\n"
        "    sed -i 's/^#en_US.UTF-8 UTF-8/en_US.UTF-8 UTF-8/;s/^#zh_CN.UTF-8 UTF-8/zh_CN.UTF-8 UTF-8/' /etc/locale.gen\n"
        "    locale-gen\n"
        "    printf 'LANG=%s\\n' \"$LOCALE\" > /etc/locale.conf\n"
        "    printf '%s\\n' \"$HOSTNAME_VALUE\" > /etc/hostname\n"
        "    cat > /etc/hosts <<HOSTS\n"
        "127.0.0.1 localhost\n"
        "::1 localhost\n"
        "127.0.1.1 $HOSTNAME_VALUE.localdomain $HOSTNAME_VALUE\n"
        "HOSTS\n"
        "    printf 'KEYMAP=us\\n' > /etc/vconsole.conf\n"
        "    sed -i 's/^#Color$/Color/' /etc/pacman.conf\n"
        "    pacman -Syy --noconfirm\n"
        "    printf '\\nSet the root password.\\n'\n"
        "    set_account_password root\n"
        "}\n\n"
        "install_core_packages() {\n"
        "    local packages=(\n"
        "        zsh zsh-completions zsh-autosuggestions zsh-syntax-highlighting grml-zsh-config\n"
        "        networkmanager iwd dhcpcd dhclient\n"
        "        efivar efitools efibootmgr sbsigntools mokutil\n"
        "    )\n"
        "    [[ \"$IS_LAPTOP\" != true ]] || packages+=(tlp)\n"
        "    pacman_install \"${packages[@]}\"\n"
        "}\n\n"
        "install_drivers() {\n"
        "    if [[ \"$INTEL_GRAPHICS\" == true ]]; then\n"
        "        pacman_install vulkan-intel intel-media-driver intel-gpu-tools\n"
        "    fi\n"
        "    if [[ \"$NVIDIA_GRAPHICS\" == true ]]; then\n"
        "        sed -i -E 's/^MODULES=.*/MODULES=(nvidia nvidia_modeset nvidia_uvm nvidia_drm)/' /etc/mkinitcpio.conf\n"
        "        sed -i -E '/^HOOKS=/s/(^|[ (])kms([ )]|$)/\\1\\2/' /etc/mkinitcpio.conf\n"
        "        pacman_install nvidia-open-dkms nvidia-utils vdpauinfo\n"
        "    fi\n"
        "    if [[ \"$HAS_BLUETOOTH\" == true ]]; then\n"
        "        pacman_install bluez bluez-utils wireless-regdb\n"
        "    fi\n"
        "}\n\n");
    writer_puts(writer,
        "install_desktop() {\n"
        "    local fonts=(\n"
        "        noto-fonts noto-fonts-cjk noto-fonts-emoji noto-fonts-extra\n"
        "        ttf-sarasa-gothic ttf-jetbrains-mono ttf-dejavu\n"
        "        ttf-nerd-fonts-symbols ttf-nerd-fonts-symbols-mono\n"
        "    )\n"
        "    case \"$DESKTOP\" in\n"
        "        kde)\n"
        "            pacman_install plasma sddm-kcm\n"
        "            if [[ \"$DESKTOP_RECOMMENDED\" == true ]]; then\n"
        "                pacman_install konsole dolphin ark kate partitionmanager filelight kcalc \\\n"
        "                    gwenview okular kcharselect ksystemlog kompare kid3 haruna\n"
        "            fi\n"
        "            if [[ \"$CHINESE_INPUT\" == true ]]; then\n"
        "                pacman_install fcitx5-im fcitx5-chinese-addons\n"
        "                cat >> /etc/environment <<'FCITX'\n"
        "XMODIFIERS=@im=fcitx\n"
        "SDL_IM_MODULE=fcitx\n"
        "GLFW_IM_MODULE=ibus\n"
        "FCITX\n"
        "            fi\n"
        "            ;;\n"
        "        gnome)\n"
        "            pacman_install gnome gdm\n"
        "            [[ \"$IS_LAPTOP\" != true ]] || pacman_install tlp-pd\n"
        "            if [[ \"$DESKTOP_RECOMMENDED\" == true ]]; then\n"
        "                pacman_install dconf-editor gnome-tweaks file-roller gnome-shell-extension-appindicator\n"
        "            fi\n"
        "            if [[ \"$CHINESE_INPUT\" == true ]]; then\n"
        "                pacman_install ibus ibus-libpinyin\n"
        "            fi\n"
        "            ;;\n"
        "        hyprland)\n"
        "            pacman_install \\\n"
        "                uwsm greetd greetd-regreet hyprland hyprpolkitagent hyprpaper \\\n"
        "                hyprpicker hyprshutdown waybar cliphist wofi playerctl brightnessctl \\\n"
        "                libnotify pavucontrol network-manager-applet blueman mako pipewire \\\n"
        "                pipewire-jack pipewire-alsa pipewire-pulse wireplumber \\\n"
        "                xdg-desktop-portal xdg-desktop-portal-hyprland xdg-desktop-portal-gtk \\\n"
        "                xdg-user-dirs wl-clipboard grim slurp swayimg kvantum kvantum-qt5 \\\n"
        "                nwg-look qt5-wayland qt6-wayland qt5ct qt6ct thunar gvfs gvfs-smb \\\n"
        "                gvfs-mtp tumbler ffmpegthumbnailer file-roller thunar-archive-plugin \\\n"
        "                thunar-media-tags-plugin papirus-icon-theme materia-gtk-theme kvantum-theme-materia\n"
        "            if [[ \"$CHINESE_INPUT\" == true ]]; then\n"
        "                pacman_install fcitx5-im fcitx5-chinese-addons\n"
        "                cat >> /etc/environment <<'FCITX'\n"
        "XMODIFIERS=@im=fcitx\n"
        "SDL_IM_MODULE=fcitx\n"
        "GLFW_IM_MODULE=ibus\n"
        "FCITX\n"
        "            fi\n"
        "            install -d /etc/greetd\n"
        "            cat > /etc/greetd/config.toml <<'GREETD'\n"
        "[terminal]\n"
        "vt = 1\n\n"
        "[default_session]\n"
        "command = \"dbus-run-session start-hyprland -- -c /etc/greetd/hyprland.lua\"\n"
        "user = \"greeter\"\n"
        "GREETD\n"
        "            cat > /etc/greetd/hyprland.lua <<'HYPRLAND'\n"
        "hl.monitor({ output = \"\", mode = \"highrr\", position = \"auto\", scale = \"auto\" })\n"
        "hl.on(\"hyprland.start\", function()\n"
        "    hl.exec_cmd(\"regreet; hyprctl dispatch 'hl.dsp.exit()'\")\n"
        "end)\n"
        "hl.config({ misc = { disable_hyprland_logo = true, disable_splash_rendering = true, disable_hyprland_guiutils_check = true } })\n"
        "HYPRLAND\n"
        "            cat >> /etc/greetd/regreet.toml <<'REGREET'\n\n"
        "[GTK]\n"
        "theme_name = \"Materia\"\n"
        "icon_theme_name = \"Papirus\"\n"
        "font_name = \"Noto Sans 12\"\n"
        "application_prefer_dark_theme = true\n"
        "REGREET\n"
        "            ;;\n"
        "        none) ;;\n"
        "    esac\n");
    writer_puts(writer,
        "    pacman_install \"${fonts[@]}\"\n"
        "    cat > /etc/fonts/local.conf <<'FONTCONFIG'\n"
        "<fontconfig>\n"
        "  <alias><family>sans-serif</family><prefer><family>Noto Sans</family><family>Noto Sans CJK SC</family><family>Noto Color Emoji</family><family>Symbols Nerd Font</family><family>DejaVu Sans</family></prefer></alias>\n"
        "  <alias><family>serif</family><prefer><family>Noto Serif</family><family>Noto Serif CJK SC</family><family>Noto Color Emoji</family><family>Symbols Nerd Font</family><family>DejaVu Serif</family></prefer></alias>\n"
        "  <alias><family>monospace</family><prefer><family>JetBrains Mono</family><family>Sarasa Mono SC</family><family>Noto Sans Mono</family><family>Noto Color Emoji</family><family>Symbols Nerd Font Mono</family><family>DejaVu Sans Mono</family></prefer></alias>\n"
        "</fontconfig>\n"
        "FONTCONFIG\n"
        "}\n\n"
        "install_optional_software() {\n"
        "    [[ \"$ENABLE_FIREWALL\" != true ]] || pacman_install firewalld\n"
        "    [[ \"$ENABLE_PRINTER\" != true ]] || pacman_install cups system-config-printer\n"
        "    [[ \"$INSTALL_ARCHIVE_TOOLS\" != true ]] || pacman_install unrar 7zip zip unzip\n"
        "    [[ \"$INSTALL_TERMINAL_TOOLS\" != true ]] || pacman_install \\\n"
        "        git openssh htop nvtop tmux lynx wget aria2 rsync usbutils cmus\n"
        "    [[ \"$INSTALL_EXTRA_TOOLS\" != true ]] || pacman_install \\\n"
        "        kitty neovim neovide lua51 luarocks fd ripgrep wl-clipboard npm vim mpv\n"
        "    [[ \"$INSTALL_DESKTOP_APPS\" != true ]] || pacman_install \\\n"
        "        chromium thunderbird libreoffice-fresh gimp\n"
        "}\n\n");
    writer_puts(writer,
        "configure_bootloader() {\n"
        "    bootctl --no-variables install\n"
        "    if [[ \"$ENABLE_SECURE_BOOT\" == true ]]; then\n"
        "        install -d /boot/EFI/BOOT /boot/EFI/ARCH\n"
        "        {\n"
        "            printf '\\xff\\xfe'\n"
        "            printf 'SHIMX64.EFI,Arch Linux,,Arch Linux Secure Boot\\r\\n' | iconv -f UTF-8 -t UTF-16LE\n"
        "        } > /boot/EFI/ARCH/BOOTX64.CSV\n"
        "    fi\n"
        "    if [[ -n \"$MICROCODE_FILE\" ]]; then\n"
        "        cat > /boot/loader/entries/arch.conf <<ENTRY\n"
        "title Arch Linux\n"
        "linux /$KERNEL_FILE\n"
        "initrd /$MICROCODE_FILE\n"
        "initrd /$INITRAMFS_FILE\n"
        "options root=UUID=$ROOT_UUID rw loglevel=3\n"
        "ENTRY\n"
        "        cat > /boot/loader/entries/arch-fallback.conf <<ENTRY\n"
        "title Arch Linux Fallback\n"
        "linux /$KERNEL_FILE\n"
        "initrd /$MICROCODE_FILE\n"
        "initrd /$FALLBACK_FILE\n"
        "options root=UUID=$ROOT_UUID rw loglevel=3\n"
        "ENTRY\n"
        "    else\n"
        "        cat > /boot/loader/entries/arch.conf <<ENTRY\n"
        "title Arch Linux\n"
        "linux /$KERNEL_FILE\n"
        "initrd /$INITRAMFS_FILE\n"
        "options root=UUID=$ROOT_UUID rw loglevel=3\n"
        "ENTRY\n"
        "        cat > /boot/loader/entries/arch-fallback.conf <<ENTRY\n"
        "title Arch Linux Fallback\n"
        "linux /$KERNEL_FILE\n"
        "initrd /$FALLBACK_FILE\n"
        "options root=UUID=$ROOT_UUID rw loglevel=3\n"
        "ENTRY\n"
        "    fi\n"
        "    cat > /boot/loader/loader.conf <<'LOADER'\n"
        "default arch.conf\n"
        "editor no\n"
        "timeout 3\n"
        "console-mode keep\n"
        "LOADER\n"
        "}\n\n");
    writer_puts(writer,
        "configure_system() {\n"
        "    install -d /etc/NetworkManager/conf.d /etc/systemd/timesyncd.conf.d \\\n"
        "        /etc/systemd/coredump.conf.d /etc/systemd/journald.conf.d\n"
        "    cat > /etc/NetworkManager/conf.d/wifi_backend.conf <<'NETWORK'\n"
        "[device]\n"
        "wifi.backend=iwd\n"
        "NETWORK\n"
        "    cat > /etc/systemd/timesyncd.conf.d/local.conf <<'TIME'\n"
        "[Time]\n"
        "NTP=cn.ntp.org.cn time.windows.com cn.pool.ntp.org time.cloudflare.com\n"
        "TIME\n"
        "    cat > /etc/systemd/coredump.conf.d/custom.conf <<'COREDUMP'\n"
        "[Coredump]\n"
        "Storage=none\n"
        "ProcessSizeMax=0\n"
        "COREDUMP\n"
        "    cat > /etc/systemd/journald.conf.d/custom.conf <<'JOURNAL'\n"
        "[Journal]\n"
        "SystemMaxUse=500M\n"
        "SystemMaxFileSize=50M\n"
        "JOURNAL\n"
        "    systemctl enable NetworkManager.service systemd-timesyncd.service fstrim.timer\n"
        "    [[ \"$HAS_BLUETOOTH\" != true ]] || systemctl enable bluetooth.service\n"
        "    [[ \"$IS_LAPTOP\" != true ]] || systemctl enable tlp.service\n"
        "    [[ \"$ENABLE_FIREWALL\" != true ]] || systemctl enable firewalld.service\n"
        "    [[ \"$ENABLE_PRINTER\" != true ]] || systemctl enable cups.socket\n"
        "    case \"$DESKTOP\" in\n"
        "        kde) systemctl enable sddm.service ;;\n"
        "        gnome) systemctl enable gdm.service ;;\n"
        "        hyprland) systemctl enable greetd.service ;;\n"
        "    esac\n"
        "    useradd -m -G wheel -s /bin/zsh \"$USERNAME\"\n"
        "    install -d -m 0750 /etc/sudoers.d\n"
        "    printf '%%wheel ALL=(ALL:ALL) ALL\\n' > /etc/sudoers.d/10-wheel\n"
        "    chmod 0440 /etc/sudoers.d/10-wheel\n"
        "    visudo -cf /etc/sudoers\n"
        "    printf '\\nSet the password for %s.\\n' \"$USERNAME\"\n"
        "    set_account_password \"$USERNAME\"\n"
        "}\n\n"
        "configure_mirrors() {\n"
        "    [[ \"$USE_CHINA_MIRRORS\" == true ]] || return 0\n"
        "    cat > /etc/pacman.d/mirrorlist <<'MIRRORS'\n"
        "Server = https://mirrors.tuna.tsinghua.edu.cn/archlinux/$repo/os/$arch\n"
        "Server = https://mirrors.ustc.edu.cn/archlinux/$repo/os/$arch\n"
        "Server = https://mirrors.bfsu.edu.cn/archlinux/$repo/os/$arch\n"
        "Server = https://mirrors.nju.edu.cn/archlinux/$repo/os/$arch\n"
        "Server = https://mirrors.sjtug.sjtu.edu.cn/archlinux/$repo/os/$arch\n"
        "MIRRORS\n"
        "}\n\n"
        "configure_base\n"
        "install_core_packages\n"
        "install_drivers\n"
        "install_desktop\n"
        "install_optional_software\n"
        "mkinitcpio -P\n"
        "configure_bootloader\n"
        "configure_system\n"
        "configure_mirrors\n"
        "printf '\\nChroot configuration complete.\\n'\n"
        "ARCH_CHROOT_SCRIPT\n"
        "    chmod 0700 \"$TARGET_ROOT/root/.arch-install-chroot.sh\"\n"
        "}\n\n");
    return writer->ok;
}

static void emit_outer_finish(ScriptWriter *writer, const InstallPlan *plan)
{
    emit_boolean(writer, "IS_LAPTOP", plan->system.laptop);
    emit_boolean(writer, "ENABLE_SECURE_BOOT", plan->system.secure_boot);
    emit_boolean(writer, "USE_CHINA_MIRRORS", plan->system.china_mirrors);
    writer_puts(writer,
        "atomic_install_file() {\n"
        "    local source=$1 destination=$2 mode=$3 directory stage stage_index\n"
        "    directory=${destination%/*}\n"
        "    [[ -d \"$directory\" && ! -L \"$directory\" ]] ||\n"
        "        die \"Secure Boot destination is not a real directory: $directory\"\n"
        "    stage=$(mktemp \"$directory/.arch-install-stage.XXXXXX\") ||\n"
        "        die \"Cannot stage Secure Boot file in $directory\"\n"
        "    SECURE_BOOT_STAGED_FILES+=(\"$stage\")\n"
        "    stage_index=$((${#SECURE_BOOT_STAGED_FILES[@]} - 1))\n"
        "    install -m \"$mode\" -- \"$source\" \"$stage\"\n"
        "    mv -fT -- \"$stage\" \"$destination\"\n"
        "    SECURE_BOOT_STAGED_FILES[stage_index]=''\n"
        "}\n\n"
        "discard_secure_boot_snapshot() {\n"
        "    local snapshot_identity\n"
        "    [[ -n \"$SECURE_BOOT_ASSET_SNAPSHOT\" ]] || return 0\n"
        "    if [[ \"$SECURE_BOOT_SNAPSHOT_MOUNTED\" == true ]]; then\n"
        "        snapshot_identity=$(findmnt -rn --mountpoint \\\n"
        "            \"$SECURE_BOOT_ASSET_SNAPSHOT\" -o SOURCE,FSTYPE) ||\n"
        "            die 'Cannot verify the private Secure Boot snapshot mount.'\n"
        "        [[ \"$snapshot_identity\" == 'tmpfs tmpfs' ]] ||\n"
        "            die 'Refusing to unmount an unexpected Secure Boot snapshot source.'\n"
        "        umount -- \"$SECURE_BOOT_ASSET_SNAPSHOT\" ||\n"
        "            die 'Cannot unmount the private Secure Boot snapshot.'\n"
        "        SECURE_BOOT_SNAPSHOT_MOUNTED=false\n"
        "    fi\n"
        "    rmdir -- \"$SECURE_BOOT_ASSET_SNAPSHOT\" ||\n"
        "        die 'Cannot remove the private Secure Boot snapshot directory.'\n"
        "    SECURE_BOOT_ASSET_SNAPSHOT=''\n"
        "}\n\n");
    writer_puts(writer,
        "sign_secure_boot_assets() {\n"
        "    local boot_binary kernel_original boot_signed kernel_signed secure_root path\n"
        "    [[ \"$ENABLE_SECURE_BOOT\" == true ]] || return 0\n"
        "    secure_root=$SECURE_BOOT_ASSET_SNAPSHOT\n"
        "    verify_secure_boot_assets \"$secure_root\"\n"
        "    require_command sbsign\n"
        "    require_command sbverify\n"
        "    boot_binary=$TARGET_ROOT/boot/EFI/systemd/systemd-bootx64.efi\n"
        "    kernel_original=$TARGET_ROOT/boot/$KERNEL_IMAGE\n"
        "    boot_signed=$WORK_DIR/systemd-boot.signed.efi\n"
        "    kernel_signed=$WORK_DIR/kernel.signed.efi\n"
        "    [[ -f \"$boot_binary\" && ! -L \"$boot_binary\" ]] ||\n"
        "        die 'Unsigned systemd-boot binary is missing.'\n"
        "    [[ -f \"$kernel_original\" && ! -L \"$kernel_original\" ]] ||\n"
        "        die 'Unsigned kernel image is missing.'\n"
        "    for path in \"$TARGET_ROOT/boot\" \"$TARGET_ROOT/boot/EFI\" \\\n"
        "                \"$TARGET_ROOT/boot/EFI/BOOT\" \"$TARGET_ROOT/boot/EFI/ARCH\"; do\n"
        "        [[ -d \"$path\" && ! -L \"$path\" ]] ||\n"
        "            die \"Secure Boot path is not a real directory: $path\"\n"
        "    done\n"
        "    phase 'Signing boot files outside the target chroot'\n"
        "    sbsign --key \"$secure_root/secure-boot/MOK.key\" \\\n"
        "        --cert \"$secure_root/secure-boot/MOK.crt\" \\\n"
        "        --output \"$boot_signed\" \"$boot_binary\"\n"
        "    sbsign --key \"$secure_root/secure-boot/MOK.key\" \\\n"
        "        --cert \"$secure_root/secure-boot/MOK.crt\" \\\n"
        "        --output \"$kernel_signed\" \"$kernel_original\"\n"
        "    sbverify --cert \"$secure_root/secure-boot/MOK.crt\" \"$boot_signed\" >/dev/null\n"
        "    sbverify --cert \"$secure_root/secure-boot/MOK.crt\" \"$kernel_signed\" >/dev/null\n");
    writer_puts(writer,
        "    atomic_install_file \"$kernel_signed\" \"$kernel_original\" 0644\n"
        "    atomic_install_file \"$boot_signed\" \\\n"
        "        \"$TARGET_ROOT/boot/EFI/BOOT/GRUBX64.EFI\" 0644\n"
        "    atomic_install_file \"$boot_signed\" \\\n"
        "        \"$TARGET_ROOT/boot/EFI/ARCH/GRUBX64.EFI\" 0644\n"
        "    atomic_install_file \"$secure_root/mmx64.efi\" \\\n"
        "        \"$TARGET_ROOT/boot/EFI/BOOT/MMX64.EFI\" 0644\n"
        "    atomic_install_file \"$secure_root/fbx64.efi\" \\\n"
        "        \"$TARGET_ROOT/boot/EFI/BOOT/FBX64.EFI\" 0644\n"
        "    atomic_install_file \"$secure_root/mmx64.efi\" \\\n"
        "        \"$TARGET_ROOT/boot/EFI/ARCH/MMX64.EFI\" 0644\n"
        "    atomic_install_file \"$secure_root/fbx64.efi\" \\\n"
        "        \"$TARGET_ROOT/boot/EFI/ARCH/FBX64.EFI\" 0644\n"
        "    atomic_install_file \"$secure_root/secure-boot/MOK.cer\" \\\n"
        "        \"$TARGET_ROOT/boot/Arch_Linux_Secure_Boot_Key.cer\" 0644\n"
        "    atomic_install_file \"$secure_root/shimx64.efi\" \\\n"
        "        \"$TARGET_ROOT/boot/EFI/ARCH/SHIMX64.EFI\" 0644\n"
        "    atomic_install_file \"$secure_root/shimx64.efi\" \\\n"
        "        \"$TARGET_ROOT/boot/EFI/BOOT/BOOTX64.EFI\" 0644\n"
        "    SECURE_BOOT_SIGNING_COMPLETE=true\n"
        "    discard_secure_boot_snapshot\n"
        "}\n\n"
        "finalize_target_package_config() {\n"
        "    [[ \"$USE_LOCAL_MIRROR\" == true ]] || return 0\n"
        "    cp -a -- \"$WORK_DIR/target-pacman.conf\" \"$TARGET_ROOT/etc/pacman.conf\"\n"
        "    if [[ \"$USE_CHINA_MIRRORS\" != true ]]; then\n"
        "        cp -a -- \"$WORK_DIR/target-mirrorlist\" \"$TARGET_ROOT/etc/pacman.d/mirrorlist\"\n"
        "    fi\n"
        "    TARGET_CONFIG_FINALIZED=true\n"
        "}\n\n"
        "create_firmware_entry() {\n"
        "    local part_number loader label entries boot_partuuid entry\n"
        "    [[ \"$CREATE_EFI_ENTRY\" == true ]] || return 0\n"
        "    part_number=$(lsblk -dnro PARTN -- \"$BOOT_DEVICE\")\n"
        "    [[ \"$part_number\" =~ ^[0-9]+$ ]] || die 'Cannot determine the EFI partition number.'\n"
        "    if [[ \"$ENABLE_SECURE_BOOT\" == true ]]; then\n"
        "        loader='\\EFI\\ARCH\\SHIMX64.EFI'\n"
        "        label='Arch Linux'\n"
        "    else\n"
        "        loader='\\EFI\\systemd\\systemd-bootx64.efi'\n"
        "        label='Linux Boot Manager'\n"
        "    fi\n"
        "    boot_partuuid=$(lsblk -dnro PARTUUID -- \"$BOOT_DEVICE\") ||\n"
        "        die 'Cannot determine the EFI partition PARTUUID.'\n"
        "    [[ -n \"$boot_partuuid\" ]] || die 'The EFI partition has no PARTUUID.'\n"
        "    entries=$(arch-chroot \"$TARGET_ROOT\" efibootmgr -v) ||\n"
        "        die 'Cannot read existing EFI boot entries.'\n"
        "    while IFS= read -r entry; do\n"
        "        if [[ \"$entry\" == *\"$label\"* &&\n"
        "              \"${entry,,}\" == *\"${boot_partuuid,,}\"* &&\n"
        "              \"${entry,,}\" == *\"${loader,,}\"* ]]; then\n"
        "            printf 'The matching EFI entry already exists; not creating a duplicate.\\n'\n"
        "            return 0\n"
        "        fi\n"
        "    done <<<\"$entries\"\n"
        "    arch-chroot \"$TARGET_ROOT\" efibootmgr --create --disk \"$TARGET_DISK\" \\\n"
        "        --part \"$part_number\" --loader \"$loader\" --label \"$label\" --unicode\n"
        "}\n\n"
        "main() {\n"
        "    WORK_DIR=$(/usr/bin/mktemp -d /tmp/arch-install.XXXXXX) ||\n"
        "        die 'Cannot create the private installer work directory.'\n"
        "    chmod 0700 \"$WORK_DIR\"\n"
        "    phase 'Preflight checks'\n"
        "    preflight\n"
        "    print_plan\n"
        "    confirm_package_preparation\n"
        "    prepare_package_source\n"
        "    snapshot_secure_boot_assets\n"
        "    probe_kept_filesystems\n"
        "    confirm_destructive_actions\n"
        "    phase 'Rechecking storage immediately before writes'\n"
        "    verify_storage_state\n"
        "    partition_disk\n"
        "    wait_for_partitions\n"
        "    format_partitions\n"
        "    mount_filesystems\n"
        "    install_base_system\n"
        "    write_chroot_script\n"
        "    phase 'Configuring the installed system'\n"
        "    arch-chroot \"$TARGET_ROOT\" /bin/bash /root/.arch-install-chroot.sh\n"
        "    unmount_target_local_mirror\n"
        "    sign_secure_boot_assets\n"
        "    finalize_target_package_config\n"
        "    create_firmware_entry\n"
        "    rm -f -- \"$TARGET_ROOT/root/.arch-install-chroot.sh\"\n"
        "    sync\n"
        "    INSTALL_SUCCEEDED=true\n"
        "    phase 'Installation configured; cleaning up mounts'\n"
        "    printf 'Review the final cleanup result and log before rebooting: %s\\n' \"$LOG_FILE\"\n"
        "}\n\n"
        "main \"$@\"\n");
}

bool generate_install_script(const InstallPlan *plan, const char *path,
                             char *error, size_t error_size)
{
    ValidationReport report;
    ScriptWriter writer;
    FILE *file = NULL;
    char *temporary = NULL;
    int descriptor = -1;
    int saved_errno;
    int path_result;
    struct stat path_status;

    if (error != NULL && error_size > 0) {
        error[0] = '\0';
    }
    if (plan == NULL || path == NULL || path[0] == '\0') {
        if (error != NULL && error_size > 0) {
            (void)snprintf(error, error_size, "plan and output path are required");
        }
        return false;
    }
    validate_plan(plan, &report);
    if (report.error_count != 0) {
        const char *message = "installation plan is invalid";

        for (size_t index = 0; index < report.count; ++index) {
            if (report.issues[index].severity == ISSUE_ERROR) {
                message = report.issues[index].message;
                break;
            }
        }
        if (error != NULL && error_size > 0) {
            (void)snprintf(error, error_size, "cannot generate script: %s", message);
        }
        return false;
    }
    if (plan->storage.disk_size_bytes == 0) {
        if (error != NULL && error_size > 0) {
            (void)snprintf(error, error_size, "cannot generate script: target disk size is unknown");
        }
        return false;
    }

    path_result = lstat(path, &path_status);
    if (path_result == 0) {
        if (S_ISREG(path_status.st_mode)) {
            /* A successful atomic rename below will replace this regular file. */
        } else {
            if (error != NULL && error_size > 0) {
                (void)snprintf(error, error_size,
                               "refusing to replace non-regular output path: %s", path);
            }
            return false;
        }
    } else if (errno != ENOENT) {
        if (error != NULL && error_size > 0) {
            (void)snprintf(error, error_size, "cannot inspect %s: %s", path, strerror(errno));
        }
        return false;
    }
    temporary = malloc(strlen(path) + 16);
    if (temporary == NULL) {
        if (error != NULL && error_size > 0) {
            (void)snprintf(error, error_size, "out of memory while creating output path");
        }
        return false;
    }
    (void)snprintf(temporary, strlen(path) + 16, "%s.tmp.XXXXXX", path);
    descriptor = mkstemp(temporary);
    if (descriptor < 0) {
        saved_errno = errno;
        if (error != NULL && error_size > 0) {
            (void)snprintf(error, error_size, "cannot create output near %s: %s",
                           path, strerror(saved_errno));
        }
        free(temporary);
        return false;
    }
    if (fchmod(descriptor, S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IXGRP) != 0) {
        saved_errno = errno;
        (void)close(descriptor);
        (void)unlink(temporary);
        if (error != NULL && error_size > 0) {
            (void)snprintf(error, error_size, "cannot set output permissions: %s",
                           strerror(saved_errno));
        }
        free(temporary);
        return false;
    }
    file = fdopen(descriptor, "w");
    if (file == NULL) {
        saved_errno = errno;
        (void)close(descriptor);
        (void)unlink(temporary);
        if (error != NULL && error_size > 0) {
            (void)snprintf(error, error_size, "cannot open temporary output: %s",
                           strerror(saved_errno));
        }
        free(temporary);
        return false;
    }
    writer.file = file;
    writer.ok = true;

    (void)emit_header_and_plan(&writer, plan);
    emit_outer_runtime(&writer);
    (void)emit_chroot_configuration(&writer, plan);
    emit_outer_finish(&writer, plan);

    if (!writer.ok || fflush(file) != 0 || fsync(fileno(file)) != 0) {
        saved_errno = errno;
        (void)fclose(file);
        (void)unlink(temporary);
        if (error != NULL && error_size > 0) {
            (void)snprintf(error, error_size, "cannot write %s: %s", path,
                           strerror(saved_errno == 0 ? EIO : saved_errno));
        }
        free(temporary);
        return false;
    }
    if (fclose(file) != 0) {
        saved_errno = errno;
        (void)unlink(temporary);
        if (error != NULL && error_size > 0) {
            (void)snprintf(error, error_size, "cannot close %s: %s", path,
                           strerror(saved_errno));
        }
        free(temporary);
        return false;
    }
    if (rename(temporary, path) != 0) {
        saved_errno = errno;
        (void)unlink(temporary);
        if (error != NULL && error_size > 0) {
            (void)snprintf(error, error_size, "cannot commit %s: %s", path,
                           strerror(saved_errno));
        }
        free(temporary);
        return false;
    }
    free(temporary);
    return true;
}
