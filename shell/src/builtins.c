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
        " |          myShell -- Built-in Commands           |\n"
        " +==================================================+\n"
        " | SHELL COMMANDS                                   |\n"
        " |   exit              Exit the shell              |\n"
        " |   help              Show this help              |\n"
        " |   date              Print current date          |\n"
        " |   time              Print current time          |\n"
        " |   cd  [path]        Change working directory    |\n"
        " |   dir [path]        List directory contents     |\n"
        " +--------------------------------------------------+\n"
        " | PROCESS MANAGEMENT                               |\n"
        " |   list              List background processes   |\n"
        " |   kill  <pid>       Terminate background proc   |\n"
        " |   stop  <pid>       Suspend background proc     |\n"
        " |   resume <pid>      Resume background proc      |\n"
        " +--------------------------------------------------+\n"
        " | ENVIRONMENT (current session only)               |\n"
        " |   path              Show PATH variable          |\n"
        " |   addpath <dir>     Append dir to PATH          |\n"
        " +==================================================+\n"
        " | Append & to run a command in background         |\n"
        " | Press CTRL+C to cancel the foreground process   |\n"
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

    if (cmd->argc > 1)
        snprintf(cmdline, sizeof(cmdline), "cmd /c dir \"%s\"", cmd->argv[1]);
    else
        snprintf(cmdline, sizeof(cmdline), "cmd /c dir");

    ZeroMemory(&si, sizeof(si));
    ZeroMemory(&pi, sizeof(pi));
    si.cb = sizeof(si);

    if (CreateProcessA(NULL, cmdline, NULL, NULL, TRUE,
                        CREATE_NEW_PROCESS_GROUP, NULL, NULL, &si, &pi)) {
        g_fg_process = pi.hProcess;
        WaitForSingleObject(pi.hProcess, INFINITE);
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
    if (cmd->argc < 2) { fprintf(stderr, "Usage: kill <pid>\n"); return 0; }
    kill_process((DWORD)strtoul(cmd->argv[1], NULL, 10));
    return 0;
}

static int cmd_stop(ParsedCmd *cmd)
{
    if (cmd->argc < 2) { fprintf(stderr, "Usage: stop <pid>\n"); return 0; }
    stop_process((DWORD)strtoul(cmd->argv[1], NULL, 10));
    return 0;
}

static int cmd_resume(ParsedCmd *cmd)
{
    if (cmd->argc < 2) { fprintf(stderr, "Usage: resume <pid>\n"); return 0; }
    resume_process((DWORD)strtoul(cmd->argv[1], NULL, 10));
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
