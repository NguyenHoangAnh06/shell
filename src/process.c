#include "process.h"
#include "process_list.h"
#include "shell_utils.h"

static int append_char(char *out, int outsize, int *pos, char ch)
{
    if (*pos >= outsize - 1) return -1;
    out[(*pos)++] = ch;
    out[*pos] = '\0';
    return 0;
}

/*
 * Quote one argv element using the documented Windows argv parsing rules.
 * This keeps spaces, quotes, and trailing backslashes intact for child
 * processes using the usual CommandLineToArgv-style parser.
 */
static int append_arg(char *out, int outsize, int *pos, const char *arg)
{
    int need_quotes = (*arg == '\0') || strpbrk(arg, " \t\n\v\"") != NULL;
    const char *p;
    int backslashes = 0;

    if (!need_quotes) {
        while (*arg) {
            if (append_char(out, outsize, pos, *arg++) < 0) return -1;
        }
        return 0;
    }

    if (append_char(out, outsize, pos, '"') < 0) return -1;

    for (p = arg; *p; p++) {
        if (*p == '\\') {
            backslashes++;
            continue;
        }

        if (*p == '"') {
            while (backslashes-- > 0) {
                if (append_char(out, outsize, pos, '\\') < 0) return -1;
                if (append_char(out, outsize, pos, '\\') < 0) return -1;
            }
            if (append_char(out, outsize, pos, '\\') < 0) return -1;
            if (append_char(out, outsize, pos, '"') < 0) return -1;
            backslashes = 0;
            continue;
        }

        while (backslashes-- > 0) {
            if (append_char(out, outsize, pos, '\\') < 0) return -1;
        }
        if (append_char(out, outsize, pos, *p) < 0) return -1;
    }

    while (backslashes-- > 0) {
        if (append_char(out, outsize, pos, '\\') < 0) return -1;
        if (append_char(out, outsize, pos, '\\') < 0) return -1;
    }

    return append_char(out, outsize, pos, '"');
}

static int build_cmdline(ParsedCmd *cmd, char *out, int outsize)
{
    int i, pos = 0;
    out[0] = '\0';

    for (i = 0; i < cmd->argc; i++) {
        if (i > 0 && append_char(out, outsize, &pos, ' ') < 0) return -1;
        if (append_arg(out, outsize, &pos, cmd->argv[i]) < 0) return -1;
    }

    return 0;
}

static int is_batch(const char *name)
{
    int len = (int)strlen(name);
    if (len < 4) return 0;
    return _stricmp(name + len - 4, ".bat") == 0 ||
           _stricmp(name + len - 4, ".cmd") == 0;
}

static void wait_foreground(PROCESS_INFORMATION *pi)
{
    g_fg_process = pi->hProcess;
    g_fg_pid = pi->dwProcessId;
    WaitForSingleObject(pi->hProcess, INFINITE);
    g_fg_pid = 0;
    g_fg_process = INVALID_HANDLE_VALUE;
}

void run_process(ParsedCmd *cmd)
{
    char cmdline[MAX_CMD_LEN * 2];
    char final_cmdline[MAX_CMD_LEN * 2 + 32];
    STARTUPINFOA        si;
    PROCESS_INFORMATION pi;
    BOOL ok;

    if (build_cmdline(cmd, cmdline, sizeof(cmdline)) < 0) {
        fprintf(stderr, "%s: command line too long\n", SHELL_NAME);
        return;
    }

    if (is_batch(cmd->argv[0])) {
        if (snprintf(final_cmdline, sizeof(final_cmdline), "cmd /c %s", cmdline) >=
            (int)sizeof(final_cmdline)) {
            fprintf(stderr, "%s: command line too long\n", SHELL_NAME);
            return;
        }
    } else {
        strncpy(final_cmdline, cmdline, sizeof(final_cmdline) - 1);
        final_cmdline[sizeof(final_cmdline) - 1] = '\0';
    }

    ZeroMemory(&si, sizeof(si));
    ZeroMemory(&pi, sizeof(pi));
    si.cb = sizeof(si);

    ok = CreateProcessA(
        NULL,
        final_cmdline,
        NULL,
        NULL,
        TRUE,
        CREATE_NEW_PROCESS_GROUP,
        NULL,
        NULL,
        &si,
        &pi
    );

    if (!ok) {
        shell_perror(cmd->argv[0]);
        return;
    }

    if (!cmd->is_background) {
        wait_foreground(&pi);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        return;
    }

    CloseHandle(pi.hThread);

    {
        int i;
        for (i = 0; i < MAX_BG_PROCS; i++) {
            if (!bg_procs[i].active) {
                bg_procs[i].pid      = pi.dwProcessId;
                bg_procs[i].hProcess = pi.hProcess;
                bg_procs[i].status   = PROC_RUNNING;
                bg_procs[i].active   = 1;
                strncpy(bg_procs[i].cmd, cmdline, MAX_CMD_LEN - 1);
                bg_procs[i].cmd[MAX_CMD_LEN - 1] = '\0';
                bg_count++;
                printf("[%lu] %s &\n", (unsigned long)pi.dwProcessId, cmdline);
                return;
            }
        }
    }

    fprintf(stderr, "%s: background list full (%d slots), refusing background process\n",
            SHELL_NAME, MAX_BG_PROCS);
    TerminateProcess(pi.hProcess, 1);
    CloseHandle(pi.hProcess);
}
