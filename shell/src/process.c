#include "process.h"
#include "process_list.h"
#include "shell_utils.h"

/* ─────────────────────────────────────────────────────── */
/* Build a single flat command-line string from argv[]     */
static void build_cmdline(ParsedCmd *cmd, char *out, int outsize)
{
    int i, pos = 0;
    for (i = 0; i < cmd->argc; i++) {
        int need_quotes;
        int len;
        need_quotes = strchr(cmd->argv[i], ' ') != NULL;
        if (i > 0 && pos < outsize - 1) out[pos++] = ' ';
        if (need_quotes && pos < outsize - 1) out[pos++] = '"';
        len = (int)strlen(cmd->argv[i]);
        if (pos + len < outsize - 1) {
            memcpy(out + pos, cmd->argv[i], len);
            pos += len;
        }
        if (need_quotes && pos < outsize - 1) out[pos++] = '"';
    }
    out[pos] = '\0';
}

/* ─────────────────────────────────────────────────────── */
/* Returns 1 if the filename ends with .bat (case-insensitive) */
static int is_batch(const char *name)
{
    int len = (int)strlen(name);
    if (len < 4) return 0;
    return _stricmp(name + len - 4, ".bat") == 0;
}

/* ─────────────────────────────────────────────────────── */
void run_process(ParsedCmd *cmd)
{
    char cmdline[MAX_CMD_LEN * 2];
    char final_cmdline[MAX_CMD_LEN * 2 + 32];

    build_cmdline(cmd, cmdline, sizeof(cmdline));

    /* Prepend "cmd /c" for batch files */
    if (is_batch(cmd->argv[0])) {
        snprintf(final_cmdline, sizeof(final_cmdline), "cmd /c %s", cmdline);
    } else {
        strncpy(final_cmdline, cmdline, sizeof(final_cmdline) - 1);
        final_cmdline[sizeof(final_cmdline) - 1] = '\0';
    }

    STARTUPINFOA        si;
    PROCESS_INFORMATION pi;
    BOOL ok;
    ZeroMemory(&si, sizeof(si));
    ZeroMemory(&pi, sizeof(pi));
    si.cb = sizeof(si);

    /*
     * CREATE_NEW_PROCESS_GROUP is critical for correct CTRL+C handling:
     *   It places the child in a new console process group so that when
     *   the user presses CTRL+C, our SetConsoleCtrlHandler in main.c can
     *   intercept the event and forward CTRL_BREAK_EVENT exclusively to
     *   the child's group — without the signal also killing the shell.
     *
     *   Note: CTRL_BREAK_EVENT (not CTRL_C_EVENT) must be used when
     *   targeting a process group created with CREATE_NEW_PROCESS_GROUP,
     *   because CTRL_C_EVENT is blocked for such groups by Windows.
     */
    ok = CreateProcessA(
        NULL,           /* Application name: use cmdline */
        final_cmdline,  /* Command line */
        NULL,           /* Process security attrs */
        NULL,           /* Thread security attrs */
        TRUE,           /* Inherit handles */
        CREATE_NEW_PROCESS_GROUP, /* Creation flags — see note above */
        NULL,           /* Environment (inherit) */
        NULL,           /* Current directory (inherit) */
        &si,
        &pi
    );

    if (!ok) {
        shell_perror(cmd->argv[0]);  /* e.g. "myShell: notepad: The system cannot find..." */
        return;
    }

    if (!cmd->is_background) {
        /* ── Foreground: wait for child ── */
        g_fg_process = pi.hProcess;
        WaitForSingleObject(pi.hProcess, INFINITE);
        g_fg_process = INVALID_HANDLE_VALUE;
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    } else {
        /* ── Background: register in list ── */
        int i;
        CloseHandle(pi.hThread);  /* Thread handle not tracked; close immediately */

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
        /* List full — fall back to foreground */
        fprintf(stderr, "%s: background list full (%d slots), running in foreground\n",
                SHELL_NAME, MAX_BG_PROCS);
        g_fg_process = pi.hProcess;
        WaitForSingleObject(pi.hProcess, INFINITE);
        g_fg_process = INVALID_HANDLE_VALUE;
        CloseHandle(pi.hProcess);
        /* hThread already closed above */
    }
}
