#include "process.h"
#include "process_list.h"
#include "shell_utils.h"

#define WIDE_CMD_CAP (MAX_CMD_LEN * 4)

static int append_wchar(wchar_t *out, int outsize, int *pos, wchar_t ch)
{
    if (*pos >= outsize - 1) return -1;
    out[(*pos)++] = ch;
    out[*pos] = L'\0';
    return 0;
}

static int append_wtext(wchar_t *out, int outsize, int *pos,
                        const wchar_t *text)
{
    while (*text) {
        if (append_wchar(out, outsize, pos, *text++) < 0) return -1;
    }
    return 0;
}

/* Quote one argument according to the Microsoft C runtime argv rules. */
static int append_windows_arg(wchar_t *out, int outsize, int *pos,
                              const wchar_t *arg)
{
    int need_quotes = (*arg == L'\0') || wcspbrk(arg, L" \t\n\v\"") != NULL;
    const wchar_t *p;
    int backslashes = 0;

    if (!need_quotes)
        return append_wtext(out, outsize, pos, arg);

    if (append_wchar(out, outsize, pos, L'"') < 0) return -1;

    for (p = arg; *p; p++) {
        if (*p == L'\\') {
            backslashes++;
            continue;
        }
        if (*p == L'"') {
            while (backslashes-- > 0) {
                if (append_wtext(out, outsize, pos, L"\\\\") < 0) return -1;
            }
            if (append_wtext(out, outsize, pos, L"\\\"") < 0) return -1;
            backslashes = 0;
            continue;
        }
        while (backslashes-- > 0) {
            if (append_wchar(out, outsize, pos, L'\\') < 0) return -1;
        }
        if (append_wchar(out, outsize, pos, *p) < 0) return -1;
    }

    while (backslashes-- > 0) {
        if (append_wtext(out, outsize, pos, L"\\\\") < 0) return -1;
    }
    return append_wchar(out, outsize, pos, L'"');
}

static int build_regular_cmdline(const ParsedCmd *cmd,
                                 wchar_t *out, int outsize)
{
    int i;
    int pos = 0;
    out[0] = L'\0';

    for (i = 0; i < cmd->argc; i++) {
        wchar_t *arg = utf8_to_wide(cmd->argv[i]);
        int rc;
        if (!arg) return -1;
        if (i > 0 && append_wchar(out, outsize, &pos, L' ') < 0) {
            free(arg);
            return -1;
        }
        rc = append_windows_arg(out, outsize, &pos, arg);
        free(arg);
        if (rc < 0) return -1;
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

/*
 * cmd.exe has different quoting rules from CreateProcess. Every batch
 * argument is quoted, delayed expansion is disabled, and percent signs or
 * embedded quotes are rejected because cmd would expand/reinterpret them.
 */
static int build_batch_cmdline(const ParsedCmd *cmd,
                               wchar_t *out, int outsize)
{
    int i;
    int pos = 0;

    out[0] = L'\0';
    if (append_wtext(out, outsize, &pos,
                     L"cmd.exe /d /v:off /s /c \"") < 0)
        return -1;

    for (i = 0; i < cmd->argc; i++) {
        wchar_t *arg = utf8_to_wide(cmd->argv[i]);
        const wchar_t *p;
        if (!arg) return -1;

        for (p = arg; *p; p++) {
            int unsafe_common = *p == L'%' || *p == L'"' ||
                                *p == L'\r' || *p == L'\n';
            int unsafe_batch_arg = i > 0 &&
                wcschr(L"&|<>()^", *p) != NULL;
            if (unsafe_common || unsafe_batch_arg) {
                free(arg);
                SetLastError(ERROR_INVALID_PARAMETER);
                return -2;
            }
        }

        if (i > 0 && append_wchar(out, outsize, &pos, L' ') < 0) {
            free(arg);
            return -1;
        }
        if (append_wchar(out, outsize, &pos, L'"') < 0 ||
            append_wtext(out, outsize, &pos, arg) < 0 ||
            append_wchar(out, outsize, &pos, L'"') < 0) {
            free(arg);
            return -1;
        }
        free(arg);
    }

    return append_wchar(out, outsize, &pos, L'"');
}

static void build_display_cmd(const ParsedCmd *cmd, char *out, size_t outsize)
{
    int i;
    size_t used = 0;
    out[0] = '\0';

    for (i = 0; i < cmd->argc; i++) {
        int written = snprintf(out + used, outsize - used, "%s%s",
                               i ? " " : "", cmd->argv[i]);
        if (written < 0 || (size_t)written >= outsize - used) {
            if (outsize >= 4) {
                memcpy(out + outsize - 4, "...", 4);
            }
            return;
        }
        used += (size_t)written;
    }
}

static void wait_foreground(PROCESS_INFORMATION *pi)
{
    g_fg_process = pi->hProcess;
    g_fg_pid = pi->dwProcessId;
    WaitForSingleObject(pi->hProcess, INFINITE);
    g_fg_pid = 0;
    g_fg_process = INVALID_HANDLE_VALUE;
}

static int open_background_null_handles(HANDLE *hNullIn, HANDLE *hNullOut)
{
    SECURITY_ATTRIBUTES sa;

    sa.nLength = sizeof(sa);
    sa.lpSecurityDescriptor = NULL;
    sa.bInheritHandle = TRUE;

    *hNullIn = CreateFileW(
        L"NUL",
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        &sa,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL);
    if (*hNullIn == INVALID_HANDLE_VALUE) return -1;

    *hNullOut = CreateFileW(
        L"NUL",
        GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        &sa,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL);
    if (*hNullOut == INVALID_HANDLE_VALUE) {
        CloseHandle(*hNullIn);
        *hNullIn = INVALID_HANDLE_VALUE;
        return -1;
    }

    return 0;
}
void run_process(ParsedCmd *cmd)
{
    wchar_t cmdline[WIDE_CMD_CAP];
    char display[MAX_CMD_LEN];
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    BOOL ok;
    int build_rc;
    DWORD creation_flags = CREATE_NEW_PROCESS_GROUP;
    HANDLE hNullIn = INVALID_HANDLE_VALUE;
    HANDLE hNullOut = INVALID_HANDLE_VALUE;

    if (is_batch(cmd->argv[0]))
        build_rc = build_batch_cmdline(cmd, cmdline, WIDE_CMD_CAP);
    else
        build_rc = build_regular_cmdline(cmd, cmdline, WIDE_CMD_CAP);

    if (build_rc == -2) {
        fprintf(stderr,
                "%s: unsafe character in batch path/arguments "
                "(%%, quote, newline, or CMD metacharacter)\n",
                SHELL_NAME);
        return;
    }
    if (build_rc < 0) {
        fprintf(stderr, "%s: invalid UTF-8 or command line too long\n",
                SHELL_NAME);
        return;
    }

    build_display_cmd(cmd, display, sizeof(display));

    ZeroMemory(&si, sizeof(si));
    ZeroMemory(&pi, sizeof(pi));
    si.cb = sizeof(si);

    if (cmd->is_background) {
        if (open_background_null_handles(&hNullIn, &hNullOut) < 0) {
            shell_perror("background redirection");
            return;
        }
        si.dwFlags |= STARTF_USESTDHANDLES;
        si.hStdInput = hNullIn;
        si.hStdOutput = hNullOut;
        si.hStdError = hNullOut;
        creation_flags |= CREATE_NO_WINDOW;
    }
    ok = CreateProcessW(
        NULL,
        cmdline,
        NULL,
        NULL,
        TRUE,
        creation_flags,
        NULL,
        NULL,
        &si,
        &pi);

    if (hNullIn != INVALID_HANDLE_VALUE) {
        CloseHandle(hNullIn);
        hNullIn = INVALID_HANDLE_VALUE;
    }
    if (hNullOut != INVALID_HANDLE_VALUE) {
        CloseHandle(hNullOut);
        hNullOut = INVALID_HANDLE_VALUE;
    }
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
                bg_procs[i].pid = pi.dwProcessId;
                bg_procs[i].hProcess = pi.hProcess;
                bg_procs[i].status = PROC_RUNNING;
                bg_procs[i].active = 1;
                snprintf(bg_procs[i].cmd, sizeof(bg_procs[i].cmd),
                         "%s", display);
                bg_count++;
                printf("[%lu] %s &\n",
                       (unsigned long)pi.dwProcessId, display);
                return;
            }
        }
    }

    fprintf(stderr,
            "%s: background list full (%d slots), refusing process\n",
            SHELL_NAME, MAX_BG_PROCS);
    TerminateProcess(pi.hProcess, 1);
    WaitForSingleObject(pi.hProcess, INFINITE);
    CloseHandle(pi.hProcess);
}
