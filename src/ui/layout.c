#include "private.h"

#include <string.h>

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

/*
 * 所有表格列都从同一份描述计算位置。空间不足时优先压缩富余最多的列，
 * 仍然保留每一列和固定间距，不由页面自行隐藏字段。
 */
void calculate_table_layout(UiTableLayout *layout, int x, int available_width,
                            const UiTableColumn columns[], size_t count, int gap)
{
    int content_width;
    int total = 0;
    int cursor = x;

    if (count > UI_TABLE_MAX_COLUMNS) count = UI_TABLE_MAX_COLUMNS;
    if (gap < 0) gap = 0;
    layout->count = count;
    layout->gap = gap;
    content_width = available_width - (count > 0 ? (int)(count - 1) * gap : 0);
    if (content_width < (int)count) content_width = (int)count;

    for (size_t index = 0; index < count; ++index) {
        int minimum = columns[index].minimum_width > 0 ? columns[index].minimum_width : 1;
        int preferred = columns[index].preferred_width > minimum ?
                        columns[index].preferred_width : minimum;
        layout->widths[index] = preferred;
        total += preferred;
    }
    while (total > content_width) {
        size_t candidate = count;
        int greatest_slack = 0;

        /* 每轮只压缩富余最多的一列，使各列逐步接近自己的最小宽度。 */
        for (size_t index = 0; index < count; ++index) {
            int minimum = columns[index].minimum_width > 0 ? columns[index].minimum_width : 1;
            int slack = layout->widths[index] - minimum;
            if (slack > greatest_slack) {
                greatest_slack = slack;
                candidate = index;
            }
        }
        if (candidate == count) break;
        --layout->widths[candidate];
        --total;
    }
    /* 极窄区域仍按同一规则保留所有列，只把最小宽度继续压到一个字符。 */
    while (total > content_width) {
        size_t candidate = count;
        int greatest_width = 1;

        for (size_t index = 0; index < count; ++index) {
            if (layout->widths[index] > greatest_width) {
                greatest_width = layout->widths[index];
                candidate = index;
            }
        }
        if (candidate == count) break;
        --layout->widths[candidate];
        --total;
    }
    for (size_t index = 0; index < count; ++index) {
        layout->positions[index] = cursor;
        cursor += layout->widths[index] + gap;
    }
}

static void draw_table_cell(WINDOW *window, int y, int x, int width,
                            UiTextAlignment alignment, const char *text)
{
    size_t length;
    int visible;
    int text_x = x;
    int window_width = getmaxx(window);

    /* 所有边界裁切集中在单元格层，调用页面可以始终提交完整布局。 */
    if (width <= 0 || x >= window_width || y < 0 || y >= getmaxy(window)) return;
    if (x + width > window_width) width = window_width - x;
    if (width <= 0) return;
    if (text == NULL) text = "";
    length = strlen(text);
    visible = length > (size_t)width ? width : (int)length;
    if (alignment == UI_ALIGN_RIGHT) text_x += width - visible;
    mvwhline(window, y, x, ' ', width);
    mvwaddnstr(window, y, text_x, text, visible);
}

void draw_table_row(WINDOW *window, int y, const UiTableLayout *layout,
                    const UiTableColumn columns[], const char *const values[])
{
    for (size_t index = 0; index < layout->count; ++index) {
        draw_table_cell(window, y, layout->positions[index], layout->widths[index],
                        columns[index].alignment, values[index]);
        if (index + 1 < layout->count) {
            int gap_x = layout->positions[index] + layout->widths[index];
            if (gap_x < getmaxx(window)) mvwhline(window, y, gap_x, ' ', layout->gap);
        }
    }
}

void draw_table_header(WINDOW *window, int y, const UiTableLayout *layout,
                       const UiTableColumn columns[])
{
    const char *titles[UI_TABLE_MAX_COLUMNS];

    for (size_t index = 0; index < layout->count; ++index)
        titles[index] = columns[index].title;
    draw_table_row(window, y, layout, columns, titles);
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
