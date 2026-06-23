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

static int append_cmd_text(char *out, int outsize, int *pos, const char *text)
{
    while (*text) {
        if (*pos >= outsize - 1) return -1;
        out[(*pos)++] = *text++;
    }
    out[*pos] = '\0';
    return 0;
}

static int append_cmd_arg(char *out, int outsize, int *pos, const char *arg)
{
    int need_quotes = (*arg == '\0') || strpbrk(arg, " \t&()[]{}^=;!'+,`~") != NULL;

    if (!need_quotes) return append_cmd_text(out, outsize, pos, arg);

    if (append_cmd_text(out, outsize, pos, "\"") < 0) return -1;
    while (*arg) {
        if (*arg == '"') {
            if (append_cmd_text(out, outsize, pos, "\\\"") < 0) return -1;
        } else if (*arg == '%') {
            if (append_cmd_text(out, outsize, pos, "%%") < 0) return -1;
        } else {
            char tmp[2];
            tmp[0] = *arg;
            tmp[1] = '\0';
            if (append_cmd_text(out, outsize, pos, tmp) < 0) return -1;
        }
        arg++;
    }
    return append_cmd_text(out, outsize, pos, "\"");
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
    char buf[MAX_PATH];

    if (cmd->argc < 2) {
        /* No argument: print current directory */
        if (GetCurrentDirectoryA(MAX_PATH, buf))
            printf("%s\n", buf);
        else
            shell_perror("GetCurrentDirectory");
        return 0;
    }

    if (!SetCurrentDirectoryA(cmd->argv[1])) {
        shell_perror("cd");   /* e.g. "myShell: cd: The system cannot find the path specified. (error 3)" */
    }
    return 0;
}

static int cmd_dir(ParsedCmd *cmd)
{
    char cmdline[MAX_CMD_LEN];
    STARTUPINFOA        si;
    PROCESS_INFORMATION pi;
    int i, pos;

    snprintf(cmdline, sizeof(cmdline), "cmd /c dir");
    pos = (int)strlen(cmdline);
    for (i = 1; i < cmd->argc; i++) {
        if (append_cmd_text(cmdline, sizeof(cmdline), &pos, " ") < 0 ||
            append_cmd_arg(cmdline, sizeof(cmdline), &pos, cmd->argv[i]) < 0) {
            fprintf(stderr, "%s: dir: command line too long\n", SHELL_NAME);
            return 0;
        }
    }

    ZeroMemory(&si, sizeof(si));
    ZeroMemory(&pi, sizeof(pi));
    si.cb = sizeof(si);

    if (CreateProcessA(NULL, cmdline, NULL, NULL, TRUE,
                        CREATE_NEW_PROCESS_GROUP, NULL, NULL, &si, &pi)) {
        g_fg_process = pi.hProcess;
        g_fg_pid = pi.dwProcessId;
        WaitForSingleObject(pi.hProcess, INFINITE);
        g_fg_pid = 0;
        g_fg_process = INVALID_HANDLE_VALUE;
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    } else {
        shell_perror("dir");
    }
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
    char *p;
    char buf[32768];
    char *tok;
    (void)cmd;

    p = getenv("PATH");
    if (!p) { printf("PATH not set\n"); return 0; }

    strncpy(buf, p, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    printf("PATH (current session):\n");
    tok = strtok(buf, ";");
    while (tok) {
        printf("  %s\n", tok);
        tok = strtok(NULL, ";");
    }
    printf("  [Note: changes exist only for this session]\n");
    return 0;
}

static int cmd_addpath(ParsedCmd *cmd)
{
    char *cur;
    char newpath[32768];

    if (cmd->argc < 2) { fprintf(stderr, "Usage: addpath <directory>\n"); return 0; }

    cur = getenv("PATH");
    if (cur)
        snprintf(newpath, sizeof(newpath), "%s;%s", cur, cmd->argv[1]);
    else
        strncpy(newpath, cmd->argv[1], sizeof(newpath) - 1);
    newpath[sizeof(newpath) - 1] = '\0';

    if (!SetEnvironmentVariableA("PATH", newpath)) {
        shell_perror("addpath");
        return 0;
    }
    printf("PATH updated: '%s' added (this session only, not saved to Registry).\n",
           cmd->argv[1]);
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
