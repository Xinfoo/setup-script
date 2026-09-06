#define _POSIX_C_SOURCE 200809L

#include "atomic_file.h"
#include "packages.h"

#include "text.h"

#include <json-c/json.h>

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
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

/* 内核始终有对应的基础包组；越界值保守回退到标准 linux。 */
PackageGroup packages_kernel_group(Kernel kernel)
{
    switch (kernel) {
    case KERNEL_LINUX: return PKG_KERNEL_LINUX;
    case KERNEL_LTS: return PKG_KERNEL_LTS;
    case KERNEL_ZEN: return PKG_KERNEL_ZEN;
    case KERNEL_HARDENED: return PKG_KERNEL_HARDENED;
    }
    return PKG_KERNEL_LINUX;
}

/* VM 不需要实体 CPU 微码包，因此通过 false 明确表示“没有附加组”。 */
bool packages_platform_group(Platform platform, PackageGroup *group)
{
    if (group == NULL) return false;
    if (platform == PLATFORM_INTEL) {
        *group = PKG_PLATFORM_INTEL;
        return true;
    }
    if (platform == PLATFORM_AMD) {
        *group = PKG_PLATFORM_AMD;
        return true;
    }
    return false;
}

/* Desktop None 没有桌面基础组，调用方仍可显示一个明确的空包列表。 */
bool packages_desktop_group(Desktop desktop, PackageGroup *group)
{
    if (group == NULL) return false;
    switch (desktop) {
    case DESKTOP_KDE: *group = PKG_KDE; return true;
    case DESKTOP_GNOME: *group = PKG_GNOME; return true;
    case DESKTOP_HYPRLAND: *group = PKG_HYPRLAND; return true;
    case DESKTOP_NONE: return false;
    }
    return false;
}

/* 推荐包目前只属于 KDE 和 GNOME；Hyprland 的完整组件已经包含在基础组中。 */
bool packages_desktop_recommended_group(Desktop desktop, PackageGroup *group)
{
    if (group == NULL) return false;
    if (desktop == DESKTOP_KDE) {
        *group = PKG_KDE_RECOMMENDED;
        return true;
    }
    if (desktop == DESKTOP_GNOME) {
        *group = PKG_GNOME_RECOMMENDED;
        return true;
    }
    return false;
}

/* 输入法跟随桌面集成方式选择：GNOME 使用 IBus，其余受支持桌面使用 Fcitx。 */
bool packages_input_group(Desktop desktop, PackageGroup *group)
{
    if (group == NULL) return false;
    if (desktop == DESKTOP_GNOME) {
        *group = PKG_IBUS;
        return true;
    }
    if (desktop == DESKTOP_KDE || desktop == DESKTOP_HYPRLAND) {
        *group = PKG_FCITX;
        return true;
    }
    return false;
}

static void append_group(PackageGroupList *groups, PackageGroup group)
{
    /* 容量等于全部已知组数量；边界判断使该辅助函数面对异常调用仍保持有界。 */
    if (groups->count < PKG_GROUP_COUNT) groups->values[groups->count++] = group;
}

/*
 * 安装前预解析的软件包并集在这里按稳定顺序构造。生成器不再自行重述每个
 * SystemPlan 开关的含义，新增软件选项时只需维护这一处映射。
 */
void packages_collect_required_groups(const InstallPlan *plan, PackageGroupList *groups)
{
    const SystemPlan *system;
    PackageGroup group;

    if (groups == NULL) return;
    groups->count = 0;
    if (plan == NULL) return;
    system = &plan->system;

    /* 列表前部依次放置 bootstrap、目标系统核心、内核和平台微码。 */
    append_group(groups, PKG_BOOTSTRAP);
    append_group(groups, PKG_CORE);
    append_group(groups, packages_kernel_group(system->kernel));
    if (packages_platform_group(system->platform, &group)) append_group(groups, group);
    if (system->laptop) {
        append_group(groups, PKG_LAPTOP_FIRMWARE);
        append_group(groups, PKG_LAPTOP_TOOLS);
    }

    /* 第二阶段追加用户明确选择的硬件支持和桌面环境组件。 */
    if (system->intel_graphics) append_group(groups, PKG_INTEL_GRAPHICS);
    if (system->nvidia_graphics) append_group(groups, PKG_NVIDIA_GRAPHICS);
    if (system->bluetooth) append_group(groups, PKG_BLUETOOTH);
    if (packages_desktop_group(system->desktop, &group)) append_group(groups, group);
    if (system->desktop == DESKTOP_GNOME && system->laptop) {
        append_group(groups, PKG_GNOME_LAPTOP);
    }
    if (system->desktop_recommended &&
        packages_desktop_recommended_group(system->desktop, &group)) {
        append_group(groups, group);
    }
    if (system->chinese_input && packages_input_group(system->desktop, &group)) {
        append_group(groups, group);
    }

    /* 字体是固定基础项，其后的通用工具组严格跟随各自开关。 */
    append_group(groups, PKG_FONTS);
    if (system->firewall) append_group(groups, PKG_FIREWALL);
    if (system->printer) append_group(groups, PKG_PRINTER);
    if (system->archive_tools) append_group(groups, PKG_ARCHIVE_TOOLS);
    if (system->terminal_tools) append_group(groups, PKG_TERMINAL_TOOLS);
    if (system->extra_tools) append_group(groups, PKG_EXTRA_TOOLS);
    if (system->desktop_apps) append_group(groups, PKG_DESKTOP_APPS);

    /* Live 专用依赖只参与安装前预解析，不代表它们会常驻目标系统。 */
    if (system->local_mirror) append_group(groups, PKG_LOCAL_MIRROR_LIVE);
    if (system->secure_boot) append_group(groups, PKG_SECURE_BOOT_LIVE);
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
    const char *serialized;

    if (config == NULL || path == NULL) {
        (void)snprintf(error, error_size, "package config and path are required");
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
    serialized = json_object_to_json_string_ext(
        root, JSON_C_TO_STRING_PRETTY | JSON_C_TO_STRING_SPACED);
    /* 保留 packages.json 原有的结尾换行，提交细节交给公共原子写入器。 */
    bool saved = atomic_write_text_file(path, 0644, serialized, true,
                                        "package config", error, error_size);
    json_object_put(root);
    return saved;
}
