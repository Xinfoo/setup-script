#define _POSIX_C_SOURCE 200809L

#include "packages.h"

#include "util.h"

#include <json-c/json.h>

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* 枚举顺序与 JSON 字段名保持一一对应，配置读写共同使用这张稳定映射。 */
static const char *const group_names[PKG_GROUP_COUNT] = {
    "bootstrap", "core", "kernel_linux", "kernel_lts", "kernel_zen",
    "kernel_hardened", "platform_intel", "platform_amd", "laptop_firmware",
    "laptop_tools", "gnome_laptop", "intel_graphics", "nvidia_graphics",
    "bluetooth", "kde", "kde_recommended", "fcitx", "gnome",
    "gnome_recommended", "ibus", "hyprland", "fonts", "firewall",
    "printer", "archive_tools", "terminal_tools", "extra_tools", "desktop_apps",
    "local_mirror_live", "secure_boot_live"
};

static bool valid_package_name(const char *value)
{
    const unsigned char *cursor;

    if (value == NULL || value[0] == '\0' || value[0] == '-' ||
        strlen(value) >= AI_PACKAGE_NAME_LEN) return false;
    for (cursor = (const unsigned char *)value; *cursor != '\0'; ++cursor) {
        if (!isalnum(*cursor) && *cursor != '@' && *cursor != '.' &&
            *cursor != '_' && *cursor != '+' && *cursor != '-' &&
            *cursor != ':' && *cursor != '/') return false;
    }
    return true;
}

/* 默认表统一通过有界复制进入固定容量模型，避免各组初始化逻辑分散。 */
static void set_list(PackageConfig *config, PackageGroup group,
                     const char *const *values, size_t count)
{
    PackageList *list = &config->groups[group];

    if (count > AI_MAX_PACKAGES_PER_GROUP) count = AI_MAX_PACKAGES_PER_GROUP;
    list->count = count;
    for (size_t index = 0; index < count; ++index) {
        copy_text(list->values[index], sizeof(list->values[index]), values[index]);
    }
}

#define SET_DEFAULT(config, group, values) \
    set_list((config), (group), (values), sizeof(values) / sizeof((values)[0]))

void packages_init_defaults(PackageConfig *config)
{
    /* 这些列表是首次创建 packages.json 时使用的可编辑初始配置。 */
    static const char *const bootstrap[] = {
        "base", "base-devel", "linux-firmware", "dosfstools", "xfsprogs",
        "f2fs-tools", "exfatprogs", "btrfs-progs", "ntfsprogs", "nano", "vi",
        "man-db", "man-pages", "texinfo"
    };
    static const char *const core[] = {
        "zsh", "zsh-completions", "zsh-autosuggestions", "zsh-syntax-highlighting",
        "grml-zsh-config", "networkmanager", "iwd", "dhcpcd", "dhclient",
        "efivar", "efitools", "efibootmgr", "sbsigntools", "mokutil"
    };
    static const char *const kernel_linux[] = {"linux", "linux-headers"};
    static const char *const kernel_lts[] = {"linux-lts", "linux-lts-headers"};
    static const char *const kernel_zen[] = {"linux-zen", "linux-zen-headers"};
    static const char *const kernel_hardened[] = {
        "linux-hardened", "linux-hardened-headers"
    };
    static const char *const platform_intel[] = {"intel-ucode"};
    static const char *const platform_amd[] = {"amd-ucode"};
    static const char *const laptop_firmware[] = {"sof-firmware"};
    static const char *const laptop_tools[] = {"tlp"};
    static const char *const gnome_laptop[] = {"tlp-pd"};
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
        "dconf-editor", "gnome-tweaks", "file-roller",
        "gnome-shell-extension-appindicator"
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
    static const char *const firewall[] = {"firewalld"};
    static const char *const printer[] = {"cups", "system-config-printer"};
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
    static const char *const local_mirror_live[] = {"nginx"};
    static const char *const secure_boot_live[] = {"sbsigntools"};

    memset(config, 0, sizeof(*config));
    config->version = 2;
    SET_DEFAULT(config, PKG_BOOTSTRAP, bootstrap);
    SET_DEFAULT(config, PKG_CORE, core);
    SET_DEFAULT(config, PKG_KERNEL_LINUX, kernel_linux);
    SET_DEFAULT(config, PKG_KERNEL_LTS, kernel_lts);
    SET_DEFAULT(config, PKG_KERNEL_ZEN, kernel_zen);
    SET_DEFAULT(config, PKG_KERNEL_HARDENED, kernel_hardened);
    SET_DEFAULT(config, PKG_PLATFORM_INTEL, platform_intel);
    SET_DEFAULT(config, PKG_PLATFORM_AMD, platform_amd);
    SET_DEFAULT(config, PKG_LAPTOP_FIRMWARE, laptop_firmware);
    SET_DEFAULT(config, PKG_LAPTOP_TOOLS, laptop_tools);
    SET_DEFAULT(config, PKG_GNOME_LAPTOP, gnome_laptop);
    SET_DEFAULT(config, PKG_INTEL_GRAPHICS, intel_graphics);
    SET_DEFAULT(config, PKG_NVIDIA_GRAPHICS, nvidia_graphics);
    SET_DEFAULT(config, PKG_BLUETOOTH, bluetooth);
    SET_DEFAULT(config, PKG_KDE, kde);
    SET_DEFAULT(config, PKG_KDE_RECOMMENDED, kde_recommended);
    SET_DEFAULT(config, PKG_FCITX, fcitx);
    SET_DEFAULT(config, PKG_GNOME, gnome);
    SET_DEFAULT(config, PKG_GNOME_RECOMMENDED, gnome_recommended);
    SET_DEFAULT(config, PKG_IBUS, ibus);
    SET_DEFAULT(config, PKG_HYPRLAND, hyprland);
    SET_DEFAULT(config, PKG_FONTS, fonts);
    SET_DEFAULT(config, PKG_FIREWALL, firewall);
    SET_DEFAULT(config, PKG_PRINTER, printer);
    SET_DEFAULT(config, PKG_ARCHIVE_TOOLS, archive_tools);
    SET_DEFAULT(config, PKG_TERMINAL_TOOLS, terminal_tools);
    SET_DEFAULT(config, PKG_EXTRA_TOOLS, extra_tools);
    SET_DEFAULT(config, PKG_DESKTOP_APPS, desktop_apps);
    SET_DEFAULT(config, PKG_LOCAL_MIRROR_LIVE, local_mirror_live);
    SET_DEFAULT(config, PKG_SECURE_BOOT_LIVE, secure_boot_live);
}

#undef SET_DEFAULT

const char *package_group_name(PackageGroup group)
{
    if (group < PKG_BOOTSTRAP || group >= PKG_GROUP_COUNT) return "invalid";
    return group_names[group];
}

const PackageList *packages_get(const PackageConfig *config, PackageGroup group)
{
    if (config == NULL || group < PKG_BOOTSTRAP || group >= PKG_GROUP_COUNT) return NULL;
    return &config->groups[group];
}

bool packages_load_json(PackageConfig *config, const char *path,
                        char *error, size_t error_size)
{
    struct stat status;
    struct json_object *root = NULL;
    struct json_object *version = NULL;
    struct json_object *groups = NULL;
    PackageConfig loaded = {0};

    /* 只读取真实普通文件，避免配置路径通过特殊文件产生意外行为。 */
    if (config == NULL || path == NULL) {
        (void)snprintf(error, error_size, "package config and path are required");
        return false;
    }
    if (lstat(path, &status) != 0) {
        (void)snprintf(error, error_size, "cannot inspect package config %s: %s",
                       path, strerror(errno));
        return false;
    }
    if (!S_ISREG(status.st_mode)) {
        (void)snprintf(error, error_size, "package config is not a regular file: %s", path);
        return false;
    }
    root = json_object_from_file(path);
    if (root == NULL || !json_object_is_type(root, json_type_object)) {
        (void)snprintf(error, error_size, "package config is not valid JSON");
        if (root != NULL) json_object_put(root);
        return false;
    }
    if (!json_object_object_get_ex(root, "version", &version) ||
        !json_object_is_type(version, json_type_int) || json_object_get_int(version) != 2) {
        (void)snprintf(error, error_size, "missing or unsupported packages.version");
        json_object_put(root);
        return false;
    }
    if (!json_object_object_get_ex(root, "groups", &groups) ||
        !json_object_is_type(groups, json_type_object)) {
        (void)snprintf(error, error_size, "missing or invalid packages.groups object");
        json_object_put(root);
        return false;
    }
    /* 当前格式要求所有已知组完整存在，缺项不会静默回退到内建默认值。 */
    loaded.version = 2;
    for (int group = PKG_BOOTSTRAP; group < PKG_GROUP_COUNT; ++group) {
        const char *name = package_group_name((PackageGroup)group);
        struct json_object *array = NULL;
        size_t count;

        if (!json_object_object_get_ex(groups, name, &array) ||
            !json_object_is_type(array, json_type_array)) {
            (void)snprintf(error, error_size, "missing or invalid package group: %s", name);
            json_object_put(root);
            return false;
        }
        count = json_object_array_length(array);
        if (count > AI_MAX_PACKAGES_PER_GROUP) {
            (void)snprintf(error, error_size, "package group %s exceeds %d entries",
                           name, AI_MAX_PACKAGES_PER_GROUP);
            json_object_put(root);
            return false;
        }
        loaded.groups[group].count = count;
        for (size_t index = 0; index < count; ++index) {
            struct json_object *item = json_object_array_get_idx(array, index);
            const char *value;

            if (item == NULL || !json_object_is_type(item, json_type_string)) {
                (void)snprintf(error, error_size, "package group %s contains a non-string", name);
                json_object_put(root);
                return false;
            }
            value = json_object_get_string(item);
            if (!valid_package_name(value)) {
                (void)snprintf(error, error_size, "package group %s contains an invalid name", name);
                json_object_put(root);
                return false;
            }
            copy_text(loaded.groups[group].values[index],
                      sizeof(loaded.groups[group].values[index]), value);
        }
    }
    /* 全部字段验证成功后再一次性提交，失败不会污染调用方原配置。 */
    json_object_put(root);
    *config = loaded;
    return true;
}

bool packages_save_json(const PackageConfig *config, const char *path,
                        char *error, size_t error_size)
{
    struct json_object *root = NULL;
    struct json_object *groups = NULL;
    struct stat status;
    const char *serialized;
    char *temporary = NULL;
    int descriptor = -1;
    int path_result;
    bool result = false;

    /* 目标只允许是普通文件或尚不存在的路径，拒绝覆盖链接和其他节点。 */
    if (config == NULL || path == NULL) {
        (void)snprintf(error, error_size, "package config and path are required");
        return false;
    }
    path_result = lstat(path, &status);
    if (path_result == 0 && !S_ISREG(status.st_mode)) {
        (void)snprintf(error, error_size, "refusing to replace non-regular package config: %s", path);
        return false;
    } else if (path_result != 0 && errno != ENOENT) {
        (void)snprintf(error, error_size, "cannot inspect package config %s: %s",
                       path, strerror(errno));
        return false;
    }
    /* 序列化完整版本和全部软件包组，使后续加载可以执行严格完整性检查。 */
    root = json_object_new_object();
    groups = json_object_new_object();
    if (root == NULL || groups == NULL) {
        (void)snprintf(error, error_size, "out of memory while creating package config");
        if (root != NULL) json_object_put(root);
        if (groups != NULL) json_object_put(groups);
        return false;
    }
    json_object_object_add(root, "version", json_object_new_int(2));
    for (int group = PKG_BOOTSTRAP; group < PKG_GROUP_COUNT; ++group) {
        struct json_object *array = json_object_new_array();
        const PackageList *list = &config->groups[group];

        if (array == NULL) {
            (void)snprintf(error, error_size, "out of memory while creating package config");
            json_object_put(root);
            json_object_put(groups);
            return false;
        }
        for (size_t index = 0; index < list->count; ++index) {
            json_object_array_add(array, json_object_new_string(list->values[index]));
        }
        json_object_object_add(groups, package_group_name((PackageGroup)group), array);
    }
    json_object_object_add(root, "groups", groups);
    /* 在目标目录写入、同步并重命名临时文件，避免留下半写入配置。 */
    temporary = malloc(strlen(path) + 16);
    if (temporary == NULL) {
        (void)snprintf(error, error_size, "out of memory while saving package config");
        json_object_put(root);
        return false;
    }
    (void)snprintf(temporary, strlen(path) + 16, "%s.tmp.XXXXXX", path);
    descriptor = mkstemp(temporary);
    if (descriptor < 0) {
        (void)snprintf(error, error_size, "cannot create temporary package config: %s",
                       strerror(errno));
        goto finish;
    }
    if (fchmod(descriptor, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH) != 0) {
        (void)snprintf(error, error_size, "cannot set package config permissions: %s",
                       strerror(errno));
        goto finish;
    }
    serialized = json_object_to_json_string_ext(
        root, JSON_C_TO_STRING_PRETTY | JSON_C_TO_STRING_SPACED);
    {
        const char *cursor = serialized;
        size_t remaining = strlen(serialized);

        while (remaining > 0) {
            ssize_t written = write(descriptor, cursor, remaining);
            if (written < 0 && errno == EINTR) continue;
            if (written <= 0) {
                (void)snprintf(error, error_size, "cannot write package config: %s",
                               strerror(errno));
                goto finish;
            }
            cursor += written;
            remaining -= (size_t)written;
        }
    }
    if (write(descriptor, "\n", 1) != 1 || fsync(descriptor) != 0) {
        (void)snprintf(error, error_size, "cannot commit package config: %s", strerror(errno));
        goto finish;
    }
    if (close(descriptor) != 0) {
        descriptor = -1;
        (void)snprintf(error, error_size, "cannot close package config: %s", strerror(errno));
        goto finish;
    }
    descriptor = -1;
    if (rename(temporary, path) != 0) {
        (void)snprintf(error, error_size, "cannot replace package config %s: %s",
                       path, strerror(errno));
        goto finish;
    }
    result = true;

finish:
    /* 失败路径关闭描述符并删除临时文件，原目标文件保持不变。 */
    if (descriptor >= 0) (void)close(descriptor);
    if (!result && temporary != NULL) (void)unlink(temporary);
    free(temporary);
    json_object_put(root);
    return result;
}
