#define _POSIX_C_SOURCE 200809L

#include "private.h"

#include "text.h"

#include <ctype.h>
#include <string.h>

/* 将若干软件包组视为一个连续列表，弹窗无需复制或改写配置内容。 */
static size_t package_item_count(const PackageConfig *packages,
                                 const PackageGroup groups[], size_t group_count)
{
    size_t count = 0;

    for (size_t index = 0; index < group_count; ++index) {
        const PackageList *list = packages_get(packages, groups[index]);
        if (list != NULL) count += list->count;
    }
    return count;
}

static const char *package_item_at(const PackageConfig *packages,
                                   const PackageGroup groups[], size_t group_count,
                                   size_t position)
{
    /* 逐组扣减位置，把多个固定数组映射成一个只读的逻辑索引空间。 */
    for (size_t index = 0; index < group_count; ++index) {
        const PackageList *list = packages_get(packages, groups[index]);
        if (list == NULL) continue;
        if (position < list->count) return list->values[position];
        position -= list->count;
    }
    return NULL;
}

/* 只读软件包列表支持滚动，Enter 和 Esc 都会关闭并恢复底层页面。 */
void packages_dialog(const char *title, const PackageConfig *packages,
                     const PackageGroup groups[], size_t group_count)
{
    size_t count = package_item_count(packages, groups, group_count);
    size_t offset = 0;
    int width = COLS - 4;
    int height = LINES - 4;
    WINDOW *window;

    if (width > 88) width = 88;
    if (height > 24) height = 24;
    window = newwin(height, width, (LINES - height) / 2, (COLS - width) / 2);
    if (window == NULL) return;
    keypad(window, TRUE);
    wtimeout(window, 200);
    for (;;) {
        int visible = height - 6;

        werase(window);
        box(window, 0, 0);
        wattron(window, A_BOLD | COLOR_PAIR(COLOR_TITLE));
        mvwaddnstr(window, 1, 2, title, width - 4);
        wattroff(window, A_BOLD | COLOR_PAIR(COLOR_TITLE));
        if (count == 0) {
            mvwaddnstr(window, 3, 2,
                       "No additional Pacman packages for this selection.", width - 4);
        } else {
            mvwprintw(window, 2, 2, "%zu package(s) from config/packages.json", count);
            for (int line = 0; line < visible && offset + (size_t)line < count; ++line) {
                size_t position = offset + (size_t)line;
                const char *package = package_item_at(packages, groups, group_count, position);
                mvwprintw(window, line + 4, 2, "%3zu  ", position + 1);
                mvwaddnstr(window, line + 4, 7, package != NULL ? package : "", width - 9);
            }
        }
        mvwaddnstr(window, height - 2, 2,
                   "Up/Down/PgUp/PgDn scroll   Enter close   Esc close", width - 4);
        wrefresh(window);
        {
            int key = wgetch(window);
            size_t page = visible > 0 ? (size_t)visible : 1;

            if (stop_requested || key == KEY_RESIZE) break;
            if (key == ERR) continue;
            if (key == '\n' || key == KEY_ENTER || key == 27) break;
            /* offset 始终限制到“最后一页的起点”，不会滚出空白页。 */
            if (key == KEY_UP && offset > 0) --offset;
            else if (key == KEY_DOWN && offset + page < count) ++offset;
            else if (key == KEY_PPAGE) offset = offset > page ? offset - page : 0;
            else if (key == KEY_NPAGE && count > page) {
                size_t maximum = count - page;
                offset = offset + page < maximum ? offset + page : maximum;
            } else if (key == KEY_HOME) offset = 0;
            else if (key == KEY_END && count > page) offset = count - page;
        }
    }
    delwin(window);
    touchwin(stdscr);
    (void)refresh();
}

/* 通用单选列表：负责滚动可见区域，并在关闭后恢复底层 stdscr。 */
int choose_dialog(const char *title, const char *const options[], size_t count, int current)
{
    int width = COLS - 4;
    int height = (int)count + 6;
    int selected = current >= 0 && (size_t)current < count ? current : 0;
    WINDOW *window;
    if (width > 100) width = 100;
    if (width < 58) width = 58;
    if (height > LINES - 2) height = LINES - 2;
    if (width > COLS - 2) width = COLS - 2;
    window = newwin(height, width, (LINES - height) / 2, (COLS - width) / 2);
    if (window == NULL) return -1;
    keypad(window, TRUE);
    wtimeout(window, 200);
    for (;;) {
        int visible = height - 5;
        int offset = selected >= visible ? selected - visible + 1 : 0;
        werase(window);
        box(window, 0, 0);
        wattron(window, A_BOLD | COLOR_PAIR(COLOR_TITLE));
        mvwaddnstr(window, 1, 2, title, width - 4);
        wattroff(window, A_BOLD | COLOR_PAIR(COLOR_TITLE));
        for (int line = 0; line < visible && (size_t)(offset + line) < count; ++line) {
            int index = offset + line;
            if (index == selected) wattron(window, COLOR_PAIR(COLOR_SELECTED));
            mvwaddnstr(window, line + 3, 2, options[index], width - 4);
            if (index == selected) wattroff(window, COLOR_PAIR(COLOR_SELECTED));
        }
        mvwaddnstr(window, height - 2, 2, "Enter select   Esc cancel", width - 4);
        wrefresh(window);
        int key = wgetch(window);
        if (stop_requested) {
            delwin(window);
            touchwin(stdscr);
            (void)refresh();
            return -1;
        }
        if (key == KEY_RESIZE) {
            delwin(window);
            touchwin(stdscr);
            (void)refresh();
            return -1;
        }
        if (key == ERR) continue;
        if (key == KEY_UP && selected > 0) --selected;
        else if (key == KEY_DOWN && (size_t)(selected + 1) < count) ++selected;
        else if (key == '\n' || key == KEY_ENTER) {
            delwin(window);
            touchwin(stdscr);
            (void)refresh();
            return selected;
        } else if (key == 27 || key == 'q') {
            delwin(window);
            touchwin(stdscr);
            (void)refresh();
            return -1;
        }
    }
}

/*
 * 确认窗口的文本换行器。优先在空白处分行，单个超长词才按窗口宽度截断，
 * 同一算法同时用于计算窗口高度和实际绘制，避免两者行数不一致。
 */
static bool next_wrapped_line(const char **cursor, size_t width,
                              const char **start, size_t *length)
{
    const char *text = *cursor;
    size_t span = 0;
    size_t split;

    while (*text == ' ' || *text == '\t') ++text;
    if (*text == '\0') return false;
    if (*text == '\n') {
        *start = text;
        *length = 0;
        *cursor = text + 1;
        return true;
    }
    while (text[span] != '\0' && text[span] != '\n') ++span;
    if (span <= width) {
        *start = text;
        *length = span;
        *cursor = text + span + (text[span] == '\n' ? 1 : 0);
        return true;
    }

    /* 从宽度边界反向寻找空白；找不到时才硬切超长单词。 */
    split = width;
    while (split > 0 && text[split] != ' ' && text[split] != '\t') --split;
    if (split == 0) split = width;
    *start = text;
    *length = split;
    /* 输出不包含行尾空白，下一次扫描也跳过分隔空白，避免新行以空格开头。 */
    while (*length > 0 &&
           (text[*length - 1] == ' ' || text[*length - 1] == '\t')) --(*length);
    text += split;
    while (*text == ' ' || *text == '\t') ++text;
    *cursor = text;
    return true;
}

static size_t wrapped_line_count(const char *message, size_t width)
{
    const char *cursor = message;
    const char *start;
    size_t length;
    size_t count = 0;

    while (next_wrapped_line(&cursor, width, &start, &length)) ++count;
    return count > 0 ? count : 1;
}

/* 破坏性操作统一默认选中 No，方向键只在 No/Yes 之间切换。 */
bool confirm_dialog(const char *title, const char *message)
{
    const char *const options[] = {"No", "Yes"};
    int width = 68;
    int height;
    int message_lines;
    int options_y;
    int footer_y;
    WINDOW *window;
    int selected = 0;
    if (width > COLS - 2) width = COLS - 2;
    message_lines = (int)wrapped_line_count(message, (size_t)(width - 4));
    height = message_lines + 8;
    options_y = message_lines + 4;
    footer_y = height - 2;
    window = newwin(height, width, (LINES - height) / 2, (COLS - width) / 2);
    if (window == NULL) return false;
    keypad(window, TRUE);
    wtimeout(window, 200);
    for (;;) {
        werase(window);
        box(window, 0, 0);
        wattron(window, A_BOLD | COLOR_PAIR(COLOR_WARNING));
        mvwaddnstr(window, 1, 2, title, width - 4);
        wattroff(window, A_BOLD | COLOR_PAIR(COLOR_WARNING));
        {
            const char *cursor = message;
            const char *start;
            size_t length;
            int line = 0;
            while (line < message_lines &&
                   next_wrapped_line(&cursor, (size_t)(width - 4), &start, &length)) {
                mvwaddnstr(window, 3 + line, 2, start, (int)length);
                ++line;
            }
        }
        for (int index = 0; index < 2; ++index) {
            if (index == selected) wattron(window, COLOR_PAIR(COLOR_SELECTED));
            mvwprintw(window, options_y, (width - 24) / 2 + index * 12,
                      " %-5s ", options[index]);
            if (index == selected) wattroff(window, COLOR_PAIR(COLOR_SELECTED));
        }
        mvwaddnstr(window, footer_y, 2,
                   "Left/Right select   Enter confirm   Esc cancel", width - 4);
        wrefresh(window);
        int key = wgetch(window);
        if (stop_requested) {
            delwin(window);
            touchwin(stdscr);
            return false;
        }
        if (key == KEY_RESIZE) {
            delwin(window);
            touchwin(stdscr);
            return false;
        }
        if (key == ERR) continue;
        if (key == KEY_LEFT || key == KEY_RIGHT || key == '\t') selected = 1 - selected;
        else if (key == '\n' || key == KEY_ENTER) {
            delwin(window);
            touchwin(stdscr);
            return selected == 1;
        } else if (key == 27 || key == 'q') {
            delwin(window);
            touchwin(stdscr);
            return false;
        }
    }
}

/* 单行文本编辑器：维护独立缓冲区，确认后才写回调用方提供的值。 */
bool text_dialog(const char *title, char *value, size_t size)
{
    int width = 68;
    int height = 8;
    WINDOW *window;
    char buffer[AI_PATH_LEN];
    size_t length;
    size_t cursor;
    if (size > sizeof(buffer)) size = sizeof(buffer);
    copy_text(buffer, size, value);
    if (width > COLS - 2) width = COLS - 2;
    window = newwin(height, width, (LINES - height) / 2, (COLS - width) / 2);
    if (window == NULL) return false;
    keypad(window, TRUE);
    wtimeout(window, 200);
    length = strlen(buffer);
    cursor = length;
    curs_set(1);
    for (;;) {
        int field_width = width - 4;
        /* 文本超出输入框后让视口跟随光标，缓冲区内容本身不被截断。 */
        size_t view_start = cursor >= (size_t)field_width ? cursor - (size_t)field_width + 1 : 0;
        werase(window);
        box(window, 0, 0);
        wattron(window, A_BOLD | COLOR_PAIR(COLOR_TITLE));
        mvwaddnstr(window, 1, 2, title, width - 4);
        wattroff(window, A_BOLD | COLOR_PAIR(COLOR_TITLE));
        mvwhline(window, 3, 2, ' ', field_width);
        mvwaddnstr(window, 3, 2, buffer + view_start, field_width);
        mvwaddnstr(window, 6, 2, "Enter apply   Esc cancel   Ctrl-U clear", width - 4);
        wmove(window, 3, 2 + (int)(cursor - view_start));
        wrefresh(window);
        int key = wgetch(window);
        if (stop_requested) {
            curs_set(0);
            delwin(window);
            touchwin(stdscr);
            return false;
        }
        if (key == KEY_RESIZE) {
            curs_set(0);
            delwin(window);
            touchwin(stdscr);
            return false;
        }
        if (key == ERR) continue;
        if (key == '\n' || key == KEY_ENTER) break;
        if (key == 27) {
            curs_set(0);
            delwin(window);
            touchwin(stdscr);
            return false;
        }
        if ((key == KEY_BACKSPACE || key == 127 || key == '\b') && cursor > 0) {
            /* memmove 连同结尾 NUL 一起移动，编辑过程中缓冲区始终是合法字符串。 */
            memmove(buffer + cursor - 1, buffer + cursor, length - cursor + 1);
            --cursor;
            --length;
        } else if (key == KEY_DC && cursor < length) {
            memmove(buffer + cursor, buffer + cursor + 1, length - cursor);
            --length;
        } else if (key == KEY_LEFT && cursor > 0) {
            --cursor;
        } else if (key == KEY_RIGHT && cursor < length) {
            ++cursor;
        } else if (key == 21) {
            buffer[0] = '\0';
            cursor = length = 0;
        } else if (key >= 0 && key <= 127 && isprint((unsigned char)key) && length + 1 < size) {
            memmove(buffer + cursor + 1, buffer + cursor, length - cursor + 1);
            buffer[cursor++] = (char)key;
            ++length;
        }
    }
    curs_set(0);
    delwin(window);
    touchwin(stdscr);
    if (buffer[0] == '\0') return false;
    copy_text(value, size, buffer);
    return true;
}
