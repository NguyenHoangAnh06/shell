#include "line_editor.h"
#include "builtins.h"
#include "shell_utils.h"
#include <wctype.h>

#define HISTORY_CAPACITY 100

static wchar_t *history[HISTORY_CAPACITY];
static int history_count = 0;

static const char *EXTERNAL_COMMANDS[] = {
    "cmd", "git", "notepad", "ping", "powershell", "python", NULL
};

static int starts_with_i(const wchar_t *text, const wchar_t *prefix)
{
    while (*prefix) {
        if (towlower(*text++) != towlower(*prefix++))
            return 0;
    }
    return 1;
}

static int command_matches(const char *candidate, const wchar_t *prefix)
{
    wchar_t *wide = utf8_to_wide(candidate);
    int matches = wide && starts_with_i(wide, prefix);
    free(wide);
    return matches;
}

static const char *find_completion(const wchar_t *prefix)
{
    const char *const *builtins = builtin_names();
    const char *match = NULL;
    int i;

    if (!*prefix) return NULL;

    for (i = 0; builtins[i]; i++) {
        if (!command_matches(builtins[i], prefix)) continue;
        if (match) return NULL; /* ambiguous */
        match = builtins[i];
    }
    for (i = 0; EXTERNAL_COMMANDS[i]; i++) {
        if (!command_matches(EXTERNAL_COMMANDS[i], prefix)) continue;
        if (match) return NULL; /* ambiguous */
        match = EXTERNAL_COMMANDS[i];
    }
    return match;
}

static wchar_t *completion_for_line(const wchar_t *line,
                                    size_t length,
                                    size_t cursor)
{
    const char *completion;

    if (length == 0 || cursor != length || wcspbrk(line, L" \t") != NULL)
        return NULL;

    completion = find_completion(line);
    if (!completion) return NULL;
    return utf8_to_wide(completion);
}

static COORD coord_after(COORD start, size_t cells, SHORT width)
{
    LONG absolute = (LONG)start.X + (LONG)cells;
    COORD result;
    result.X = (SHORT)(absolute % width);
    result.Y = (SHORT)(start.Y + absolute / width);
    return result;
}

static void redraw(HANDLE output,
                   COORD start,
                   const wchar_t *line,
                   size_t length,
                   size_t cursor,
                   int show_suggestion,
                   size_t *previous_cells)
{
    CONSOLE_SCREEN_BUFFER_INFO info;
    wchar_t *completion = show_suggestion
        ? completion_for_line(line, length, cursor)
        : NULL;
    const wchar_t *suffix = NULL;
    size_t suffix_length = 0;
    size_t total_cells;
    DWORD written;
    WORD normal_attributes;
    WORD ghost_attributes;
    COORD cursor_position;

    if (!GetConsoleScreenBufferInfo(output, &info)) {
        free(completion);
        return;
    }

    if (completion && wcslen(completion) > length) {
        suffix = completion + length;
        suffix_length = wcslen(suffix);
    }
    total_cells = length + suffix_length;

    FillConsoleOutputCharacterW(output, L' ',
        (DWORD)(*previous_cells > total_cells
            ? *previous_cells : total_cells),
        start, &written);
    SetConsoleCursorPosition(output, start);

    if (length > 0)
        WriteConsoleW(output, line, (DWORD)length, &written, NULL);

    normal_attributes = info.wAttributes;
    ghost_attributes = (WORD)(
        (normal_attributes & 0xFFF0) | FOREGROUND_INTENSITY);

    if (suffix_length > 0) {
        SetConsoleTextAttribute(output, ghost_attributes);
        WriteConsoleW(output, suffix, (DWORD)suffix_length, &written, NULL);
        SetConsoleTextAttribute(output, normal_attributes);
    }

    cursor_position = coord_after(start, cursor, info.dwSize.X);
    SetConsoleCursorPosition(output, cursor_position);
    *previous_cells = total_cells;
    free(completion);
}

static void reset_history_navigation(int *history_index,
                                     wchar_t *prefix,
                                     const wchar_t *line)
{
    *history_index = -1;
    wcscpy(prefix, line);
}

static int find_history_backward(int start, const wchar_t *prefix)
{
    int i;
    for (i = start; i >= 0; i--) {
        if (starts_with_i(history[i], prefix))
            return i;
    }
    return -1;
}

static int find_history_forward(int start, const wchar_t *prefix)
{
    int i;
    for (i = start; i < history_count; i++) {
        if (starts_with_i(history[i], prefix))
            return i;
    }
    return -1;
}

static void add_history(const wchar_t *line)
{
    wchar_t *copy;
    int i;

    if (!*line) return;
    if (history_count > 0 &&
        _wcsicmp(history[history_count - 1], line) == 0)
        return;

    copy = _wcsdup(line);
    if (!copy) return;

    if (history_count == HISTORY_CAPACITY) {
        free(history[0]);
        for (i = 1; i < HISTORY_CAPACITY; i++)
            history[i - 1] = history[i];
        history_count--;
    }
    history[history_count++] = copy;
}

static int finish_line(char *out, size_t outsize, const wchar_t *line)
{
    char *utf8 = wide_to_utf8(line);
    size_t length;

    if (!utf8) return -1;
    length = strlen(utf8);
    if (length >= outsize) {
        free(utf8);
        fprintf(stderr, "%s: command too long (max %d bytes)\n",
                SHELL_NAME, MAX_CMD_LEN - 1);
        return 2;
    }
    memcpy(out, utf8, length + 1);
    free(utf8);
    add_history(line);
    return 1;
}

int read_console_line(char *out, size_t outsize)
{
    HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
    HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD old_mode;
    DWORD new_mode;
    CONSOLE_SCREEN_BUFFER_INFO info;
    COORD start;
    wchar_t line[MAX_CMD_LEN];
    wchar_t history_prefix[MAX_CMD_LEN];
    size_t length = 0;
    size_t cursor = 0;
    size_t previous_cells = 0;
    int history_index = -1;

    line[0] = L'\0';
    history_prefix[0] = L'\0';

    if (!GetConsoleMode(input, &old_mode) ||
        !GetConsoleScreenBufferInfo(output, &info))
        return -1;

    start = info.dwCursorPosition;
    new_mode = old_mode;
    new_mode &= ~(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT |
                  ENABLE_PROCESSED_INPUT);
    new_mode |= ENABLE_EXTENDED_FLAGS | ENABLE_INSERT_MODE;
    if (!SetConsoleMode(input, new_mode))
        return -1;

    for (;;) {
        INPUT_RECORD record;
        DWORD count;
        KEY_EVENT_RECORD key;
        wchar_t ch;

        if (!ReadConsoleInputW(input, &record, 1, &count)) {
            SetConsoleMode(input, old_mode);
            return -1;
        }
        if (record.EventType != KEY_EVENT ||
            !record.Event.KeyEvent.bKeyDown)
            continue;

        key = record.Event.KeyEvent;
        ch = key.uChar.UnicodeChar;

        if ((key.dwControlKeyState &
             (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)) &&
            (key.wVirtualKeyCode == 'C' || ch == 3 ||
             ch == L'c' || ch == L'C')) {
            DWORD written;
            redraw(output, start, L"", 0, 0, 0, &previous_cells);
            WriteConsoleW(output, L"^C\r\n", 4, &written, NULL);
            SetConsoleMode(input, old_mode);
            out[0] = '\0';
            return 2;
        }

        if (key.wVirtualKeyCode == VK_RETURN) {
            DWORD written;
            redraw(output, start, line, length, length, 0, &previous_cells);
            WriteConsoleW(output, L"\r\n", 2, &written, NULL);
            SetConsoleMode(input, old_mode);
            return finish_line(out, outsize, line);
        }

        if (key.wVirtualKeyCode == VK_TAB) {
            wchar_t *completion = completion_for_line(line, length, cursor);
            if (completion) {
                size_t completed_length = wcslen(completion);
                if (completed_length < MAX_CMD_LEN) {
                    wcscpy(line, completion);
                    length = completed_length;
                    cursor = length;
                    reset_history_navigation(
                        &history_index, history_prefix, line);
                }
                free(completion);
            }
            redraw(output, start, line, length, cursor, 1, &previous_cells);
            continue;
        }

        if (key.wVirtualKeyCode == VK_UP) {
            int found;
            if (history_index < 0)
                wcscpy(history_prefix, line);
            found = find_history_backward(
                history_index < 0 ? history_count - 1 : history_index - 1,
                history_prefix);
            if (found >= 0) {
                history_index = found;
                wcscpy(line, history[found]);
                length = wcslen(line);
                cursor = length;
            }
            redraw(output, start, line, length, cursor, 1, &previous_cells);
            continue;
        }

        if (key.wVirtualKeyCode == VK_DOWN) {
            int found = history_index >= 0
                ? find_history_forward(history_index + 1, history_prefix)
                : -1;
            if (found >= 0) {
                history_index = found;
                wcscpy(line, history[found]);
            } else if (history_index >= 0) {
                history_index = -1;
                wcscpy(line, history_prefix);
            }
            length = wcslen(line);
            cursor = length;
            redraw(output, start, line, length, cursor, 1, &previous_cells);
            continue;
        }

        if (key.wVirtualKeyCode == VK_LEFT) {
            if (cursor > 0) cursor--;
            redraw(output, start, line, length, cursor, 1, &previous_cells);
            continue;
        }
        if (key.wVirtualKeyCode == VK_RIGHT) {
            if (cursor < length) {
                cursor++;
            } else {
                wchar_t *completion =
                    completion_for_line(line, length, cursor);
                if (completion && wcslen(completion) > length &&
                    length < MAX_CMD_LEN - 1) {
                    line[length] = completion[length];
                    length++;
                    line[length] = L'\0';
                    cursor = length;
                }
                free(completion);
            }
            redraw(output, start, line, length, cursor, 1, &previous_cells);
            continue;
        }
        if (key.wVirtualKeyCode == VK_HOME) {
            cursor = 0;
            redraw(output, start, line, length, cursor, 1, &previous_cells);
            continue;
        }
        if (key.wVirtualKeyCode == VK_END) {
            cursor = length;
            redraw(output, start, line, length, cursor, 1, &previous_cells);
            continue;
        }

        if (key.wVirtualKeyCode == VK_BACK) {
            if (cursor > 0) {
                memmove(&line[cursor - 1], &line[cursor],
                        (length - cursor + 1) * sizeof(wchar_t));
                cursor--;
                length--;
                reset_history_navigation(
                    &history_index, history_prefix, line);
            }
            redraw(output, start, line, length, cursor, 1, &previous_cells);
            continue;
        }
        if (key.wVirtualKeyCode == VK_DELETE) {
            if (cursor < length) {
                memmove(&line[cursor], &line[cursor + 1],
                        (length - cursor) * sizeof(wchar_t));
                length--;
                reset_history_navigation(
                    &history_index, history_prefix, line);
            }
            redraw(output, start, line, length, cursor, 1, &previous_cells);
            continue;
        }

        if (ch == 26) { /* CTRL+Z */
            DWORD written;
            WriteConsoleW(output, L"\r\n", 2, &written, NULL);
            SetConsoleMode(input, old_mode);
            return 0;
        }

        if (ch >= L' ' && length < MAX_CMD_LEN - 1) {
            memmove(&line[cursor + 1], &line[cursor],
                    (length - cursor + 1) * sizeof(wchar_t));
            line[cursor++] = ch;
            length++;
            reset_history_navigation(&history_index, history_prefix, line);
            redraw(output, start, line, length, cursor, 1, &previous_cells);
        }
    }
}
