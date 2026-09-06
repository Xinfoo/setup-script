#ifndef ARCH_INSTALLER_UI_PRIVATE_H
#define ARCH_INSTALLER_UI_PRIVATE_H

#include "ui.h"

#include <ncurses.h>

#include <signal.h>
#include <stdbool.h>
#include <stddef.h>

typedef enum {
    SCREEN_HOME,
    SCREEN_STORAGE,
    SCREEN_BASE_SYSTEM,
    SCREEN_HARDWARE,
    SCREEN_SOFTWARE,
    SCREEN_IDENTITY,
    SCREEN_REVIEW,
    SCREEN_OUTPUT_PREVIEW
} Screen;

/* 页面只通过 UiState 交换会话状态，不直接拥有模型或配置对象。 */
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

enum {
    COLOR_TITLE = 1,
    COLOR_SELECTED,
    COLOR_OK,
    COLOR_WARNING,
    COLOR_ERROR,
    COLOR_MUTED
};

extern volatile sig_atomic_t stop_requested;

/* 控制器与布局公共入口。 */
void set_status(UiState *state, const char *message);
void put_clipped(int y, int x, int width, const char *text);
void draw_shell(UiState *state, const char *title, const char *keys);
void draw_property_row(int y, int index, int selected,
                       const char *name, const char *value);
bool terminal_too_small(void);
void quit_builder(UiState *state);
bool ui_generate(UiState *state);

/* 模态窗口。 */
int choose_dialog(const char *title, const char *const options[],
                  size_t count, int current);
bool confirm_dialog(const char *title, const char *message);
bool text_dialog(const char *title, char *value, size_t size);
void packages_dialog(const char *title, const PackageConfig *packages,
                     const PackageGroup groups[], size_t group_count);

/* 页面路由入口。 */
void draw_home(UiState *state);
void handle_home(UiState *state, int key);
void draw_storage(UiState *state);
void handle_storage(UiState *state, int key);
void draw_base_system(UiState *state);
void handle_base_system(UiState *state, int key);
void draw_hardware(UiState *state);
void handle_hardware(UiState *state, int key);
void draw_software(UiState *state);
void handle_software(UiState *state, int key);
void draw_identity(UiState *state);
void handle_identity(UiState *state, int key);
void draw_review(UiState *state);
void handle_review(UiState *state, int key);
void draw_output_preview(UiState *state);
void handle_output_preview(UiState *state, int key);
void handle_output_request(UiState *state, int key);

#endif
