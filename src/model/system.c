#define _POSIX_C_SOURCE 200809L

#include "model.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

const char *platform_name(Platform value)
{
    static const char *const names[] = {"Intel", "AMD", "Virtual machine"};
    return value >= PLATFORM_INTEL && value <= PLATFORM_VM ? names[value] : "unknown";
}

const char *kernel_name(Kernel value)
{
    static const char *const names[] = {"linux", "linux-lts", "linux-zen", "linux-hardened"};
    return value >= KERNEL_LINUX && value <= KERNEL_HARDENED ? names[value] : "unknown";
}

const char *desktop_name(Desktop value)
{
    static const char *const names[] = {"KDE Plasma", "GNOME", "Hyprland (experimental)", "None"};
    return value >= DESKTOP_KDE && value <= DESKTOP_NONE ? names[value] : "unknown";
}

const char *locale_name(LocaleChoice value)
{
    return value == LOCALE_ZH_CN ? "zh_CN.UTF-8" : "en_US.UTF-8";
}

/* 主机名和用户名共享基本字符规则，用户名另有限定首字符与大小写。 */
static bool valid_name(const char *value, size_t maximum, bool username)
{
    size_t length;
    if (value == NULL || value[0] == '\0') {
        return false;
    }
    length = strlen(value);
    if (length > maximum || value[0] == '-' || value[length - 1] == '-') {
        return false;
    }
    if (username && !(islower((unsigned char)value[0]) || value[0] == '_')) {
        return false;
    }
    for (size_t index = 0; index < length; ++index) {
        unsigned char ch = (unsigned char)value[index];
        if (!(isalnum(ch) || ch == '-' || ch == '_')) {
            return false;
        }
        if (username && isupper(ch)) {
            return false;
        }
    }
    return true;
}

bool valid_hostname(const char *value)
{
    return valid_name(value, 63, false);
}

bool valid_username(const char *value)
{
    /* 排除目标系统常见的预置服务账户，避免创建用户时发生身份冲突。 */
    static const char *const reserved[] = {
        "root", "bin", "daemon", "mail", "ftp", "http", "nobody", "dbus",
        "systemd-journal-remote", "systemd-network", "systemd-oom", "systemd-resolve",
        "systemd-timesync", "tss", "uuidd", "dnsmasq", "rpc", "avahi", "colord",
        "cups", "flatpak", "geoclue", "git", "nm-openvpn", "openvpn", "polkitd",
        "rtkit", "sddm", "gdm", "greeter"
    };

    if (!valid_name(value, 32, true)) return false;
    for (size_t index = 0; index < sizeof(reserved) / sizeof(reserved[0]); ++index) {
        if (strcmp(value, reserved[index]) == 0) return false;
    }
    return true;
}

bool valid_timezone(const char *value)
{
    char path[256];
    int written;
    struct stat status;
    size_t length;
    /* 先限制为相对区域名，再确认对应 zoneinfo 文件真实存在且可读。 */
    if (value == NULL || value[0] == '/' || strstr(value, "..") != NULL) {
        return false;
    }
    length = strlen(value);
    if (length == 0 || length >= 120 || strchr(value, '/') == NULL) {
        return false;
    }
    for (size_t index = 0; index < length; ++index) {
        unsigned char ch = (unsigned char)value[index];
        if (!(isalnum(ch) || ch == '/' || ch == '_' || ch == '-' || ch == '+')) {
            return false;
        }
    }
    written = snprintf(path, sizeof(path), "/usr/share/zoneinfo/%s", value);
    if (written < 0 || (size_t)written >= sizeof(path)) return false;
    return stat(path, &status) == 0 && S_ISREG(status.st_mode) && access(path, R_OK) == 0;
}
