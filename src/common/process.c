#define _POSIX_C_SOURCE 200809L

#include "process.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

bool run_capture_stdout(const char *program, char *const argv[], ProcessResult *result,
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
        null_descriptor = open("/dev/null", O_WRONLY);
        if (null_descriptor < 0 || dup2(null_descriptor, STDERR_FILENO) < 0) {
            _exit(126);
        }
        (void)close(null_descriptor);
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

void process_result_free(ProcessResult *result)
{
    free(result->output);
    result->output = NULL;
    result->status = -1;
}
