#include "builtins.h"
#include "process_list.h"
#include "shell_utils.h"

/* ─── Helper: current local time ────────────────────── */
static SYSTEMTIME get_now(void)
{
    SYSTEMTIME st;
    GetLocalTime(&st);
    return st;
}

static int parse_pid_arg(const char *text, DWORD *pid)
{
    char *end;
    unsigned long value;

    if (!text || !*text) return -1;
    value = strtoul(text, &end, 10);
    if (*end != '\0' || value == 0) return -1;
    *pid = (DWORD)value;
    return 0;
}

/* ─── Built-in table ────────────────────────────────── */
/*
 * Architecture note:
 *   This module handles two categories of commands:
 *
 *   1. Pure Shell Built-ins (exit, help, date, time, dir, cd)
 *      — Commands that must run inside the shell process itself or produce
 *        shell-level side-effects (e.g. cd changes the shell's CWD via
 *        SetCurrentDirectory; running cd as a child process would only change
 *        the child's CWD and have no effect on the shell).
 *
 *   2. Process-Management Commands (list, kill, stop, resume)
 *      — Wrappers around process_list.c functions. Kept here for dispatch
 *        convenience; could be split into a separate module if the project grows.
 *
 *   3. Environment Commands (path, addpath)
 *      — Modify/display the shell's environment variables. Changes apply only
 *        to the current shell session and are NOT persisted to the Windows
 *        Registry. Could be moved to env.c in a larger refactor.
 *
 *   Future improvement: introduce a central dispatcher struct (name + handler
 *   pointer) to eliminate the cascade of if/strcmp in run_builtin().
 */
static const char *BUILTIN_NAMES[] = {
    "exit", "help", "date", "time", "dir", "cd",
    "list", "kill", "stop", "resume",
    "path", "addpath",
    NULL
};

const char *const *builtin_names(void)
{
    return BUILTIN_NAMES;
}

int is_builtin(const ParsedCmd *cmd)
{
    int i;
    if (cmd->argc == 0) return 0;
    for (i = 0; BUILTIN_NAMES[i]; i++) {
        if (_stricmp(cmd->argv[0], BUILTIN_NAMES[i]) == 0) return 1;
    }
    return 0;
}

/* ════════════════════════════════════════════════════════
 * SECTION 1 — Pure Shell Built-ins
 * ════════════════════════════════════════════════════════ */

static int cmd_exit(ParsedCmd *cmd)
{
    (void)cmd;
    kill_all_processes();
    printf("Goodbye!\n");
    return 1; /* signal shell to quit */
}

static int cmd_help(ParsedCmd *cmd)
{
    (void)cmd;
    printf(
        "\n"
        " +==================================================+\n"
        " |          myShell -- Built-in Commands            |\n"
        " +==================================================+\n"
        " | SHELL COMMANDS                                   |\n"
        " |   exit              Exit the shell               |\n"
        " |   help              Show this help               |\n"
        " |   date              Print current date           |\n"
        " |   time              Print current time           |\n"
        " |   cd  [path]        Change working directory     |\n"
        " |   dir [path]        List directory contents      |\n"
        " +--------------------------------------------------+\n"
        " | PROCESS MANAGEMENT                               |\n"
        " |   list              List background processes    |\n"
        " |   kill  <pid>       Terminate background proc    |\n"
        " |   stop  <pid>       Suspend background proc      |\n"
        " |   resume <pid>      Resume background proc       |\n"
        " +--------------------------------------------------+\n"
        " | ENVIRONMENT (current session only)               |\n"
        " |   path              Show PATH variable           |\n"
        " |   addpath <dir>     Append dir to PATH           |\n"
        " +==================================================+\n"
        " | Append & to run a command in background          |\n"
        " | Press CTRL+C to cancel the foreground process    |\n"
        " | TAB completes; UP/DOWN search prefix history     |\n"
        " +==================================================+\n"
        "\n"
    );
    return 0;
}

static int cmd_date(ParsedCmd *cmd)
{
    (void)cmd;
    SYSTEMTIME st = get_now();
    printf("%04d-%02d-%02d\n", st.wYear, st.wMonth, st.wDay);
    return 0;
}

static int cmd_time_cmd(ParsedCmd *cmd)
{
    (void)cmd;
    SYSTEMTIME st = get_now();
    printf("%02d:%02d:%02d\n", st.wHour, st.wMinute, st.wSecond);
    return 0;
}

/*
 * cmd_cd — Change the shell's working directory.
 *
 * WHY this must be a built-in:
 *   If cd were launched as a child process (via CreateProcess), it would
 *   change the child's CWD only.  The shell's own CWD — shown in the prompt
 *   and inherited by all subsequent children — would remain unchanged.
 *   Only SetCurrentDirectory() modifies the shell process's own CWD.
 *
 * Behaviour:
 *   cd          -> print current directory
 *   cd <path>   -> change to <path>
 */
static int cmd_cd(ParsedCmd *cmd)
{
    wchar_t *path;

    if (cmd->argc < 2) {
        DWORD needed = GetCurrentDirectoryW(0, NULL);
        wchar_t *buf = needed
            ? (wchar_t *)malloc((size_t)needed * sizeof(wchar_t))
            : NULL;
        if (buf && GetCurrentDirectoryW(needed, buf)) {
            shell_print_wide(stdout, buf);
            putchar('\n');
        } else {
            shell_perror("GetCurrentDirectory");
        }
        free(buf);
        return 0;
    }

    if (cmd->argc > 2) {
        fprintf(stderr, "Usage: cd [path]\n");
        return 0;
    }

    path = utf8_to_wide(cmd->argv[1]);
    if (!path) {
        shell_perror("cd: invalid UTF-8");
        return 0;
    }
    if (!SetCurrentDirectoryW(path))
        shell_perror("cd");
    free(path);
    return 0;
}

static int cmd_dir(ParsedCmd *cmd)
{
    const char *input = cmd->argc > 1 ? cmd->argv[1] : ".";
    wchar_t *path;
    wchar_t *pattern;
    WIN32_FIND_DATAW data;
    HANDLE find;
    DWORD attrs;
    size_t len;

    if (cmd->argc > 2) {
        fprintf(stderr, "Usage: dir [path]\n");
        return 0;
    }

    path = utf8_to_wide(input);
    if (!path) {
        shell_perror("dir: invalid UTF-8");
        return 0;
    }

    attrs = GetFileAttributesW(path);
    len = wcslen(path);
    pattern = (wchar_t *)malloc((len + 4) * sizeof(wchar_t));
    if (!pattern) {
        free(path);
        fprintf(stderr, "%s: dir: out of memory\n", SHELL_NAME);
        return 0;
    }
    wcscpy(pattern, path);

    if (attrs != INVALID_FILE_ATTRIBUTES &&
        (attrs & FILE_ATTRIBUTE_DIRECTORY)) {
        if (len > 0 && path[len - 1] != L'\\' && path[len - 1] != L'/')
            wcscat(pattern, L"\\");
        wcscat(pattern, L"*");
    }

    find = FindFirstFileW(pattern, &data);
    if (find == INVALID_HANDLE_VALUE) {
        shell_perror("dir");
        free(pattern);
        free(path);
        return 0;
    }

    printf(" Directory of ");
    shell_print_wide(stdout, path);
    putchar('\n');

    do {
        ULARGE_INTEGER size;
        size.HighPart = data.nFileSizeHigh;
        size.LowPart = data.nFileSizeLow;
        if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            printf("%16s  ", "<DIR>");
        else
            printf("%16llu  ", (unsigned long long)size.QuadPart);
        shell_print_wide(stdout, data.cFileName);
        putchar('\n');
    } while (FindNextFileW(find, &data));

    if (GetLastError() != ERROR_NO_MORE_FILES)
        shell_perror("dir");

    FindClose(find);
    free(pattern);
    free(path);
    return 0;
}

/* ════════════════════════════════════════════════════════
 * SECTION 2 — Process-Management Commands
 * ════════════════════════════════════════════════════════ */

static int cmd_list(ParsedCmd *cmd)
{
    (void)cmd;
    list_processes();
    return 0;
}

static int cmd_kill(ParsedCmd *cmd)
{
    DWORD pid;
    if (cmd->argc < 2) { fprintf(stderr, "Usage: kill <pid>\n"); return 0; }
    if (parse_pid_arg(cmd->argv[1], &pid) < 0) {
        fprintf(stderr, "kill: invalid PID '%s'\n", cmd->argv[1]);
        return 0;
    }
    kill_process(pid);
    return 0;
}

static int cmd_stop(ParsedCmd *cmd)
{
    DWORD pid;
    if (cmd->argc < 2) { fprintf(stderr, "Usage: stop <pid>\n"); return 0; }
    if (parse_pid_arg(cmd->argv[1], &pid) < 0) {
        fprintf(stderr, "stop: invalid PID '%s'\n", cmd->argv[1]);
        return 0;
    }
    stop_process(pid);
    return 0;
}

static int cmd_resume(ParsedCmd *cmd)
{
    DWORD pid;
    if (cmd->argc < 2) { fprintf(stderr, "Usage: resume <pid>\n"); return 0; }
    if (parse_pid_arg(cmd->argv[1], &pid) < 0) {
        fprintf(stderr, "resume: invalid PID '%s'\n", cmd->argv[1]);
        return 0;
    }
    resume_process(pid);
    return 0;
}

/* ════════════════════════════════════════════════════════
 * SECTION 3 — Environment Commands
 *
 * IMPORTANT: Changes made by addpath are local to this shell
 * process.  They are NOT written to the Windows Registry and
 * will be lost when the shell exits.  Child processes launched
 * after addpath will inherit the updated PATH because they
 * inherit the shell's environment block.
 * ════════════════════════════════════════════════════════ */

static int cmd_path(ParsedCmd *cmd)
{
    DWORD needed;
    wchar_t *buf;
    wchar_t *cursor;
    (void)cmd;

    SetLastError(ERROR_SUCCESS);
    needed = GetEnvironmentVariableW(L"PATH", NULL, 0);
    if (needed == 0) {
        DWORD err = GetLastError();
        if (err == ERROR_ENVVAR_NOT_FOUND || err == ERROR_SUCCESS)
            printf("PATH not set\n");
        else
            shell_perror("path");
        return 0;
    }
    buf = (wchar_t *)malloc((size_t)needed * sizeof(wchar_t));
    if (!buf) {
        fprintf(stderr, "%s: path: out of memory\n", SHELL_NAME);
        return 0;
    }
    if (!GetEnvironmentVariableW(L"PATH", buf, needed)) {
        shell_perror("path");
        free(buf);
        return 0;
    }

    printf("PATH (current session):\n");
    cursor = buf;
    while (*cursor) {
        wchar_t *end = wcschr(cursor, L';');
        if (end) *end = L'\0';
        printf("  ");
        shell_print_wide(stdout, cursor);
        putchar('\n');
        if (!end) break;
        cursor = end + 1;
    }
    printf("  [Note: changes exist only for this session]\n");
    free(buf);
    return 0;
}

static int cmd_addpath(ParsedCmd *cmd)
{
    DWORD needed;
    wchar_t *dir;
    wchar_t *newpath;
    size_t cur_len;
    size_t dir_len;

    if (cmd->argc != 2) {
        fprintf(stderr, "Usage: addpath <directory>\n");
        return 0;
    }

    dir = utf8_to_wide(cmd->argv[1]);
    if (!dir) {
        shell_perror("addpath: invalid UTF-8");
        return 0;
    }

    SetLastError(ERROR_SUCCESS);
    needed = GetEnvironmentVariableW(L"PATH", NULL, 0);
    if (needed == 0 &&
        GetLastError() != ERROR_SUCCESS &&
        GetLastError() != ERROR_ENVVAR_NOT_FOUND) {
        shell_perror("addpath");
        free(dir);
        return 0;
    }
    cur_len = needed ? (size_t)needed - 1 : 0;
    dir_len = wcslen(dir);
    newpath = (wchar_t *)malloc(
        (cur_len + (cur_len ? 1 : 0) + dir_len + 1) * sizeof(wchar_t));
    if (!newpath) {
        free(dir);
        fprintf(stderr, "%s: addpath: out of memory\n", SHELL_NAME);
        return 0;
    }

    newpath[0] = L'\0';
    if (needed) {
        if (!GetEnvironmentVariableW(L"PATH", newpath, needed)) {
            shell_perror("addpath");
            free(newpath);
            free(dir);
            return 0;
        }
        wcscat(newpath, L";");
    }
    wcscat(newpath, dir);

    if (!SetEnvironmentVariableW(L"PATH", newpath)) {
        shell_perror("addpath");
        free(newpath);
        free(dir);
        return 0;
    }
    printf("PATH updated: '%s' added (this session only, not saved to Registry).\n",
           cmd->argv[1]);
    free(newpath);
    free(dir);
    return 0;
}

/* ════════════════════════════════════════════════════════
 * DISPATCH
 * ════════════════════════════════════════════════════════ */
int run_builtin(ParsedCmd *cmd)
{
    const char *name;
    if (cmd->argc == 0) return 0;
    name = cmd->argv[0];

    if (cmd->is_background) {
        fprintf(stderr, "%s: built-in command '%s' cannot run in background\n",
                SHELL_NAME, name);
        return 0;
    }

    /* Section 1 — Shell built-ins */
    if (_stricmp(name, "exit")   == 0) return cmd_exit(cmd);
    if (_stricmp(name, "help")   == 0) return cmd_help(cmd);
    if (_stricmp(name, "date")   == 0) return cmd_date(cmd);
    if (_stricmp(name, "time")   == 0) return cmd_time_cmd(cmd);
    if (_stricmp(name, "cd")     == 0) return cmd_cd(cmd);
    if (_stricmp(name, "dir")    == 0) return cmd_dir(cmd);

    /* Section 2 — Process management */
    if (_stricmp(name, "list")   == 0) return cmd_list(cmd);
    if (_stricmp(name, "kill")   == 0) return cmd_kill(cmd);
    if (_stricmp(name, "stop")   == 0) return cmd_stop(cmd);
    if (_stricmp(name, "resume") == 0) return cmd_resume(cmd);

    /* Section 3 — Environment */
    if (_stricmp(name, "path")   == 0) return cmd_path(cmd);
    if (_stricmp(name, "addpath")== 0) return cmd_addpath(cmd);

    return 0;
}
