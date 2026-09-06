#define _POSIX_C_SOURCE 200809L

#include "private.h"

#include "generator.h"
#include "text.h"

#include <errno.h>
#include <locale.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* 信号处理器只记录退出请求，ncurses 清理由主循环在正常控制流中完成。 */
volatile sig_atomic_t stop_requested = 0;
static volatile sig_atomic_t stop_signal = 0;

static void request_stop(int signal_number)
{
    stop_signal = signal_number;
    stop_requested = 1;
}

/* 状态栏、颜色和页面外框由所有页面共用。 */
void set_status(UiState *state, const char *message)
{
    copy_text(state->status, sizeof(state->status), message);
}

static void init_colors(void)
{
    if (!has_colors()) return;
    start_color();
    use_default_colors();
    init_pair(COLOR_TITLE, COLOR_CYAN, -1);
    init_pair(COLOR_SELECTED, COLOR_BLACK, COLOR_CYAN);
    init_pair(COLOR_OK, COLOR_GREEN, -1);
    init_pair(COLOR_WARNING, COLOR_YELLOW, -1);
    init_pair(COLOR_ERROR, COLOR_RED, -1);
    init_pair(COLOR_MUTED, COLOR_BLUE, -1);
}

/* 保存、生成和退出操作集中在主模块，保证所有页面使用同一套状态转换。 */
void quit_builder(UiState *state)
{
    state->quit = !state->dirty ||
        confirm_dialog("Quit builder", "Unsaved changes will be lost. Quit now?");
}

static void save_plan(UiState *state)
{
    char error[512] = {0};
    if (plan_save_json(state->plan, state->plan_path, error, sizeof(error))) {
        state->dirty = false;
        (void)snprintf(state->status, sizeof(state->status), "Saved plan to %s", state->plan_path);
    } else {
        (void)snprintf(state->status, sizeof(state->status), "Save failed: %.190s", error);
    }
}

bool ui_generate(UiState *state)
{
    ValidationReport report;
    char error[512] = {0};
    validate_plan(state->plan, &report);
    /* 先检查实时身份，再验证静态方案；两者都通过才允许落盘生成脚本。 */
    if (!state->target_identity_matches) {
        set_status(state, "Cannot generate: target disk identity changed; select the disk again.");
        return false;
    }
    if (report.error_count != 0) {
        (void)snprintf(state->status, sizeof(state->status),
                       "Cannot generate: fix %zu validation error(s).", report.error_count);
        return false;
    }
    if (!plan_save_json(state->plan, state->plan_path, error, sizeof(error))) {
        (void)snprintf(state->status, sizeof(state->status), "Save failed: %.190s", error);
        return false;
    }
    if (!generate_install_script(state->plan, state->packages, state->script_path,
                                 error, sizeof(error))) {
        (void)snprintf(state->status, sizeof(state->status), "Generation failed: %.180s", error);
        return false;
    }
    state->dirty = false;
    (void)snprintf(state->status, sizeof(state->status), "Generated %s", state->script_path);
    return true;
}

/* 页面分发器只决定调用哪个页面，不在这里处理页面自己的业务按键。 */
static void draw_current(UiState *state)
{
    if (terminal_too_small()) return;
    switch (state->screen) {
    case SCREEN_HOME: draw_home(state); break;
    case SCREEN_STORAGE: draw_storage(state); break;
    case SCREEN_BASE_SYSTEM: draw_base_system(state); break;
    case SCREEN_HARDWARE: draw_hardware(state); break;
    case SCREEN_SOFTWARE: draw_software(state); break;
    case SCREEN_IDENTITY: draw_identity(state); break;
    case SCREEN_REVIEW: draw_review(state); break;
    case SCREEN_OUTPUT_PREVIEW: draw_output_preview(state); break;
    }
    refresh();
}

static void handle_key(UiState *state, int key)
{
    if (key == KEY_F(2)) { save_plan(state); return; }
    if (key == KEY_F(5)) { state->screen = SCREEN_REVIEW; state->row = 0; return; }
    if (key == KEY_F(10)) {
        quit_builder(state);
        return;
    }
    switch (state->screen) {
    case SCREEN_HOME: handle_home(state, key); break;
    case SCREEN_STORAGE: handle_storage(state, key); break;
    case SCREEN_BASE_SYSTEM: handle_base_system(state, key); break;
    case SCREEN_HARDWARE: handle_hardware(state, key); break;
    case SCREEN_SOFTWARE: handle_software(state, key); break;
    case SCREEN_IDENTITY: handle_identity(state, key); break;
    case SCREEN_REVIEW: handle_review(state, key); break;
    case SCREEN_OUTPUT_PREVIEW: handle_output_preview(state, key); break;
    }
}

/*
 * 初始化终端并运行事件循环。循环结束后先恢复终端；只有用户明确选择
 * “生成并运行”时，才用 bash 替换 builder 进程。
 */
int run_tui(InstallPlan *plan, HardwareInventory *inventory,
            const PackageConfig *packages, const char *plan_path,
            const char *script_path)
{
    struct sigaction action;
    UiState state = {
        .plan = plan,
        .inventory = inventory,
        .packages = packages,
        .plan_path = plan_path,
        .script_path = script_path,
        .screen = SCREEN_HOME,
        .target_identity_matches = plan_storage_matches_inventory(plan, inventory)
    };
    (void)setlocale(LC_ALL, "");
    if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO)) {
        (void)fprintf(stderr, "The TUI requires an interactive terminal.\n");
        return EXIT_FAILURE;
    }
    if (initscr() == NULL) {
        (void)fprintf(stderr, "Cannot initialize ncurses.\n");
        return EXIT_FAILURE;
    }
    memset(&action, 0, sizeof(action));
    action.sa_handler = request_stop;
    sigemptyset(&action.sa_mask);
    (void)sigaction(SIGINT, &action, NULL);
    (void)sigaction(SIGTERM, &action, NULL);
    (void)sigaction(SIGHUP, &action, NULL);
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    timeout(200);
    curs_set(0);
    init_colors();
    set_status(&state, "No system changes are made until a generated script is explicitly run.");
    /* 短超时让没有键盘输入时也能及时响应信号，而无需在处理器中调用 ncurses。 */
    while (!state.quit && !stop_requested) {
        draw_current(&state);
        int key = getch();
        if (stop_requested) break;
        if (key == ERR) continue;
        if (terminal_too_small()) {
            continue;
        }
        handle_key(&state, key);
    }
    endwin();
    if (state.running && !stop_requested) {
        /* exec 保留当前终端和退出码语义，让安装脚本成为唯一前台进程。 */
        execl("/usr/bin/bash", "bash", "--", script_path, (char *)NULL);
        (void)fprintf(stderr, "Cannot execute %s: %s\n", script_path, strerror(errno));
        return EXIT_FAILURE;
    }
    if (stop_requested) return 128 + (int)stop_signal;
    return EXIT_SUCCESS;
}
