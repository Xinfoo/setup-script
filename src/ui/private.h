#ifndef ARCH_INSTALLER_UI_PRIVATE_H
#define ARCH_INSTALLER_UI_PRIVATE_H

#include "ui.h"

#include <ncurses.h>

#include <signal.h>
#include <stdbool.h>
#include <stddef.h>

/* TUI 页面编号。页面切换统一由 UiState.screen 驱动。 */
typedef enum {
    SCREEN_HOME,
    SCREEN_STORAGE,
    SCREEN_SYSTEM,
    SCREEN_HARDWARE,
    SCREEN_SOFTWARE,
    SCREEN_IDENTITY,
    SCREEN_REVIEW,
    SCREEN_PREVIEW
} Screen;

/*
 * 整个 TUI 会话共享的状态。
 * row 表示当前页面内的选择项；Storage 页面再配合 active_disk 表示跨磁盘光标。
 * running 仅在退出 ncurses 后启动生成脚本，避免子进程继承活动中的终端界面。
 */
typedef struct {
    InstallPlan *plan;
    HardwareInventory *inventory;
    const PackageConfig *packages;
    const char *plan_path;
    const char *script_path;
    Screen screen;
    int row;
    size_t active_disk;
    int review_offset;
    int preview_offset;
    bool dirty;
    bool target_identity_matches;
    bool running;
    bool quit;
    char status[256];
} UiState;

/* ncurses 颜色对编号，由 ui.c 在启动时统一初始化。 */
enum {
    COLOR_TITLE = 1,
    COLOR_SELECTED,
    COLOR_OK,
    COLOR_WARNING,
    COLOR_ERROR,
    COLOR_MUTED
};

extern volatile sig_atomic_t stop_requested;

/* 公共界面框架与跨页面共享操作。 */
void set_status(UiState *state, const char *message);
bool disk_identity_matches(const InstallPlan *plan,
                           const HardwareInventory *inventory);
void put_clipped(int y, int x, int width, const char *text);
void draw_shell(UiState *state, const char *title, const char *keys);

/* 模态窗口。返回后调用方继续负责当前页面的重绘。 */
int choose_dialog(const char *title, const char *const options[],
                  size_t count, int current);
bool confirm_dialog(const char *title, const char *message);
bool text_dialog(const char *title, char *value, size_t size);
void packages_dialog(const char *title, const PackageConfig *packages,
                     const PackageGroup groups[], size_t group_count);

void quit_builder(UiState *state);
bool generate(UiState *state);

/* 各页面的绘制函数和按键处理函数成对出现。 */
void draw_home(UiState *state);
void handle_home(UiState *state, int key);
void draw_storage(UiState *state);
void handle_storage(UiState *state, int key);
void draw_system(UiState *state);
void handle_system(UiState *state, int key);
void draw_hardware(UiState *state);
void handle_hardware(UiState *state, int key);
void draw_software(UiState *state);
void handle_software(UiState *state, int key);
void draw_identity(UiState *state);
void handle_identity(UiState *state, int key);
void draw_review(UiState *state);
void handle_review(UiState *state, int key);
void draw_preview(UiState *state);
void handle_preview(UiState *state, int key);

#endif
