#define _POSIX_C_SOURCE 200809L

#include "util.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

/* 所有模型字符串都通过该入口进行有界复制，并统一处理空指针。 */
void copy_text(char *destination, size_t size, const char *source)
{
    if (size == 0) {
        return;
    }
    if (source == NULL) {
        destination[0] = '\0';
        return;
    }
    (void)snprintf(destination, size, "%s", source);
}

static bool run_capture_internal(const char *program, char *const argv[],
                                 bool merge_standard_error, ProcessResult *result,
                                 char *error, size_t error_size)
{
    int pipefd[2];
    pid_t child;
    char *buffer = NULL;
    size_t used = 0;
    size_t capacity = 4096;
    int wait_status = 0;

    /* 父进程读取管道，子进程只负责重定向输出并执行指定的绝对路径程序。 */
    result->status = -1;
    result->output = NULL;
    if (pipe(pipefd) != 0) {
        (void)snprintf(error, error_size, "pipe: %s", strerror(errno));
        return false;
    }

    child = fork();
    if (child < 0) {
        (void)snprintf(error, error_size, "fork: %s", strerror(errno));
        (void)close(pipefd[0]);
        (void)close(pipefd[1]);
        return false;
    }
    if (child == 0) {
        int null_descriptor = -1;
        (void)close(pipefd[0]);
        if (dup2(pipefd[1], STDOUT_FILENO) < 0) {
            _exit(126);
        }
        if (merge_standard_error) {
            if (dup2(pipefd[1], STDERR_FILENO) < 0) _exit(126);
        } else {
            null_descriptor = open("/dev/null", O_WRONLY);
            if (null_descriptor < 0 || dup2(null_descriptor, STDERR_FILENO) < 0) {
                _exit(126);
            }
            (void)close(null_descriptor);
        }
        (void)close(pipefd[1]);
        /* execv 不经过 PATH 搜索或 Shell；126/127 分别保留给重定向和执行失败。 */
        execv(program, argv);
        _exit(127);
    }

    (void)close(pipefd[1]);
    buffer = malloc(capacity);
    if (buffer == NULL) {
        (void)snprintf(error, error_size, "out of memory");
        (void)close(pipefd[0]);
        (void)waitpid(child, &wait_status, 0);
        return false;
    }

    /*
     * 按需扩展并在 waitpid 前持续排空管道，否则大量输出可能填满管道，
     * 造成父进程等待子进程、子进程等待父进程读取的互锁。
     */
    for (;;) {
        ssize_t count;
        if (capacity - used < 2048) {
            size_t new_capacity = capacity * 2;
            char *resized;
            if (new_capacity < capacity) {
                free(buffer);
                (void)close(pipefd[0]);
                (void)waitpid(child, &wait_status, 0);
                (void)snprintf(error, error_size, "command output is too large");
                return false;
            }
            resized = realloc(buffer, new_capacity);
            if (resized == NULL) {
                free(buffer);
                (void)close(pipefd[0]);
                (void)waitpid(child, &wait_status, 0);
                (void)snprintf(error, error_size, "out of memory");
                return false;
            }
            buffer = resized;
            capacity = new_capacity;
        }
        count = read(pipefd[0], buffer + used, capacity - used - 1);
        if (count > 0) {
            used += (size_t)count;
            continue;
        }
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count < 0) {
            (void)snprintf(error, error_size, "read: %s", strerror(errno));
            free(buffer);
            (void)close(pipefd[0]);
            (void)waitpid(child, &wait_status, 0);
            return false;
        }
        break;
    }
    (void)close(pipefd[0]);
    buffer[used] = '\0';

    while (waitpid(child, &wait_status, 0) < 0) {
        if (errno != EINTR) {
            (void)snprintf(error, error_size, "waitpid: %s", strerror(errno));
            free(buffer);
            return false;
        }
    }
    /* 调用方主要区分成功与失败；被信号终止统一映射为非零的 128。 */
    result->status = WIFEXITED(wait_status) ? WEXITSTATUS(wait_status) : 128;
    result->output = buffer;
    return true;
}

bool run_capture(const char *program, char *const argv[], ProcessResult *result,
                 char *error, size_t error_size)
{
    return run_capture_internal(program, argv, true, result, error, error_size);
}

bool run_capture_stdout(const char *program, char *const argv[], ProcessResult *result,
                        char *error, size_t error_size)
{
    return run_capture_internal(program, argv, false, result, error, error_size);
}

void process_result_free(ProcessResult *result)
{
    free(result->output);
    result->output = NULL;
    result->status = -1;
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

char *shell_quote(const char *value)
{
    size_t length = 3;
    char *quoted;
    char *cursor;

    if (value == NULL) {
        value = "";
    }
    /* 预先计算单引号转义后的精确长度，随后构造可直接嵌入 Bash 的字面量。 */
    for (const char *p = value; *p != '\0'; ++p) {
        if (*p == '\'') {
            if (length > SIZE_MAX - 4) {
                return NULL;
            }
            length += 4;
        } else {
            if (length == SIZE_MAX) {
                return NULL;
            }
            ++length;
        }
    }
    quoted = malloc(length);
    if (quoted == NULL) {
        return NULL;
    }
    cursor = quoted;
    *cursor++ = '\'';
    for (const char *p = value; *p != '\0'; ++p) {
        if (*p == '\'') {
            memcpy(cursor, "'\\''", 4);
            cursor += 4;
        } else {
            *cursor++ = *p;
        }
    }
    *cursor++ = '\'';
    *cursor = '\0';
    return quoted;
}
