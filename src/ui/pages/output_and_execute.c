#define _POSIX_C_SOURCE 200809L

#include "private.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

void handle_output_request(UiState *state, int key)
{
    if (key == 'g' || key == 'G') {
        (void)ui_generate(state);
    } else if (key == 'p' || key == 'P') {
        if (ui_generate(state)) {
            state->screen = SCREEN_OUTPUT_PREVIEW;
            state->preview_offset = 0;
        }
    } else if (key == 'x' || key == 'X') {
        if (ui_generate(state) &&
            confirm_dialog("Run generated installer",
                           "Leave the TUI and execute the generated Bash script now?")) {
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
    if (key == 27) { state->screen = SCREEN_REVIEW; return; }
    if ((key == KEY_UP || key == KEY_PPAGE) && state->preview_offset > 0)
        state->preview_offset -= key == KEY_PPAGE ? 10 : 1;
    else if (key == KEY_DOWN) ++state->preview_offset;
    else if (key == KEY_NPAGE) state->preview_offset += 10;
    else if (key == 'g' || key == 'G') (void)ui_generate(state);
    if (state->preview_offset < 0) state->preview_offset = 0;
}
