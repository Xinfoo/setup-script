#define _POSIX_C_SOURCE 200809L

#include "private.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

/* Output 页面使用启动时确定的路径，集中管理生成、预览和执行请求。 */
void draw_output(UiState *state)
{
    draw_shell(state, "Output", "Up/Down move   Enter/Space select   Esc back");
    mvprintw(5, 4, "Output path:");
    put_clipped(5, 18, COLS - 21, state->script_path);
    draw_property_row(8, 0, state->row, "Generate script", "Save plan and write the Bash script");
    draw_property_row(10, 1, state->row, "Preview script", "Generate, then inspect the Bash output");
    draw_property_row(12, 2, state->row, "Generate and run", "Leave the TUI and start installation");
}

void handle_output(UiState *state, int key)
{
    if (key == 27) { state->screen = SCREEN_HOME; state->row = 6; return; }
    if (key == KEY_UP && state->row > 0) --state->row;
    else if (key == KEY_DOWN && state->row < 2) ++state->row;
    else if (key == ' ' || page_enter_pressed(key)) {
        /* 三个动作共用 ui_generate，确保保存方案、验证和原子输出语义完全一致。 */
        if (state->row == 0) {
            (void)ui_generate(state);
        } else if (state->row == 1) {
            if (ui_generate(state)) {
                state->screen = SCREEN_OUTPUT_PREVIEW;
                state->preview_offset = 0;
            }
        } else if (ui_generate(state) &&
                   confirm_dialog("Run generated installer",
                                  "Leave the TUI and execute the generated Bash script now?")) {
            /* 主循环退出后由顶层入口 exec 脚本，页面本身不破坏 ncurses 状态。 */
            state->running = true;
            state->quit = true;
        }
    }
}

/* 预览页面逐行读取刚生成的脚本，不把整个脚本长期保存在 UI 状态中。 */
void draw_output_preview(UiState *state)
{
    FILE *file;
    char *line = NULL;
    size_t capacity = 0;
    ssize_t length;
    int number = 0;
    int y = 4;
    draw_shell(state, "Generated Bash preview", "Up/Down scroll   G regenerate   Esc back");
    file = fopen(state->script_path, "r");
    if (file == NULL) {
        mvprintw(5, 2, "Cannot open %s: %s", state->script_path, strerror(errno));
        return;
    }
    /* 逐行跳过滚动偏移，只保留 getline 的单个复用缓冲区。 */
    while ((length = getline(&line, &capacity, file)) >= 0) {
        if (number++ < state->preview_offset) continue;
        if (y >= LINES - 3) break;
        if (length > 0 && line[length - 1] == '\n') line[length - 1] = '\0';
        mvprintw(y, 1, "%4d ", number);
        put_clipped(y++, 7, COLS - 8, line);
    }
    free(line);
    (void)fclose(file);
}

void handle_output_preview(UiState *state, int key)
{
    if (key == 27) { state->screen = SCREEN_OUTPUT; state->row = 1; return; }
    if ((key == KEY_UP || key == KEY_PPAGE) && state->preview_offset > 0)
        state->preview_offset -= key == KEY_PPAGE ? 10 : 1;
    else if (key == KEY_DOWN) ++state->preview_offset;
    else if (key == KEY_NPAGE) state->preview_offset += 10;
    else if (key == 'g' || key == 'G') (void)ui_generate(state);
    if (state->preview_offset < 0) state->preview_offset = 0;
}
