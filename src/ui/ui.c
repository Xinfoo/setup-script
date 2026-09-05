#define _POSIX_C_SOURCE 200809L

#include "private.h"

#include "generator.h"
#include "util.h"

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

void put_clipped(int y, int x, int width, const char *text)
{
    if (width <= 0 || y < 0 || y >= LINES || x >= COLS) return;
    mvaddnstr(y, x, text != NULL ? text : "", width);
}

void draw_shell(UiState *state, const char *title, const char *keys)
{
    erase();
    attron(A_BOLD | COLOR_PAIR(COLOR_TITLE));
    put_clipped(0, 2, COLS - 4, "Arch Linux Install Script Builder");
    attroff(A_BOLD | COLOR_PAIR(COLOR_TITLE));
    if (state->dirty) {
        attron(COLOR_PAIR(COLOR_WARNING));
        put_clipped(0, COLS - 13, 11, "[modified]");
        attroff(COLOR_PAIR(COLOR_WARNING));
    }
    mvhline(1, 0, ACS_HLINE, COLS);
    attron(A_BOLD);
    put_clipped(2, 2, COLS - 4, title);
    attroff(A_BOLD);
    mvhline(LINES - 3, 0, ACS_HLINE, COLS);
    attron(COLOR_PAIR(COLOR_MUTED));
    put_clipped(LINES - 2, 1, COLS - 2, keys);
    attroff(COLOR_PAIR(COLOR_MUTED));
    put_clipped(LINES - 1, 1, COLS - 2, state->status);
}

static bool terminal_too_small(void)
{
    if (COLS >= 80 && LINES >= 24) return false;
    erase();
    attron(A_BOLD | COLOR_PAIR(COLOR_ERROR));
    mvprintw(2, 2, "Terminal is too small (%dx%d).", COLS, LINES);
    mvprintw(4, 2, "Resize it to at least 80x24 to continue.");
    attroff(A_BOLD | COLOR_PAIR(COLOR_ERROR));
    refresh();
    return true;
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

bool generate(UiState *state)
{
    ValidationReport report;
    char error[512] = {0};
    validate_plan(state->plan, &report);
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
    case SCREEN_SYSTEM: draw_system(state); break;
    case SCREEN_HARDWARE: draw_hardware(state); break;
    case SCREEN_SOFTWARE: draw_software(state); break;
    case SCREEN_IDENTITY: draw_identity(state); break;
    case SCREEN_REVIEW: draw_review(state); break;
    case SCREEN_PREVIEW: draw_preview(state); break;
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
    case SCREEN_SYSTEM: handle_system(state, key); break;
    case SCREEN_HARDWARE: handle_hardware(state, key); break;
    case SCREEN_SOFTWARE: handle_software(state, key); break;
    case SCREEN_IDENTITY: handle_identity(state, key); break;
    case SCREEN_REVIEW: handle_review(state, key); break;
    case SCREEN_PREVIEW: handle_preview(state, key); break;
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
        .target_identity_matches = disk_identity_matches(plan, inventory)
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
        execl("/usr/bin/bash", "bash", "--", script_path, (char *)NULL);
        (void)fprintf(stderr, "Cannot execute %s: %s\n", script_path, strerror(errno));
        return EXIT_FAILURE;
    }
    if (stop_requested) return 128 + (int)stop_signal;
    return EXIT_SUCCESS;
}
