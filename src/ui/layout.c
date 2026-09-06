#include "private.h"

/* 输出自动裁切到当前终端边界，页面无需重复处理右侧越界。 */
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

void draw_property_row(int y, int index, int selected,
                       const char *name, const char *value)
{
    if (index == selected) attron(COLOR_PAIR(COLOR_SELECTED));
    mvprintw(y, 4, " %-28s ", name);
    if (index == selected) attroff(COLOR_PAIR(COLOR_SELECTED));
    put_clipped(y, 36, COLS - 39, value);
}

bool terminal_too_small(void)
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
