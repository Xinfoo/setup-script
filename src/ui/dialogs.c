#define _POSIX_C_SOURCE 200809L

#include "private.h"

#include "util.h"

#include <ctype.h>
#include <string.h>

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

    split = width;
    while (split > 0 && text[split] != ' ' && text[split] != '\t') --split;
    if (split == 0) split = width;
    *start = text;
    *length = split;
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
