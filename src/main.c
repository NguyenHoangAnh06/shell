/*
 * main.c  —  myShell entry point
 *
 * REPL loop:
 *   1. Print prompt
 *   2. Read a line
 *   3. Parse it
 *   4. Dispatch: builtin or external process
 *   5. Repeat
 *
 * CTRL+C handler:
 *   - If a foreground process is running, send it CTRL_BREAK_EVENT and leave
 *     the shell alive.
 *   - Otherwise, ignore (do not kill the shell).
 */

#include "shell.h"
#include "parser.h"
#include "process.h"
#include "process_list.h"
#include "builtins.h"
#include "shell_utils.h"


/* ─────────────────────────────────────────────────────── */
static BOOL WINAPI ctrl_handler(DWORD ctrl_type)
{
    if (ctrl_type == CTRL_C_EVENT || ctrl_type == CTRL_BREAK_EVENT) {
        DWORD pid = g_fg_pid;
        if (pid != 0) {
            /* Kill only the foreground child, keep the shell alive */
            if (!GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, pid)) {
                shell_perror("CTRL+C");
            }
            return TRUE;  /* handled */
        }
        /* No foreground process — just print a newline, re-show prompt */
        printf("\n");
        return TRUE;
    }
    return FALSE;
}

/* ─────────────────────────────────────────────────────── */
static void print_prompt(void)
{
    char cwd[MAX_PATH];
    if (GetCurrentDirectoryA(MAX_PATH, cwd))
        printf("\n[%s]> ", cwd);
    else
        printf("\n%s> ", SHELL_NAME);
    fflush(stdout);
}

/* ─────────────────────────────────────────────────────── */
static void print_banner(void)
{
    printf(
        "\n"
        " +==========================================+\n"
        " |          myShell -- Tiny Windows Shell   |\n"
        " |    Type 'help' to see all commands       |\n"
        " +==========================================+\n"
        "\n"
    );
}

/* ─────────────────────────────────────────────────────── */
int main(void)
{
    char line[MAX_CMD_LEN];
    ParsedCmd cmd;

    /* Register CTRL+C / CTRL+BREAK handler */
    SetConsoleCtrlHandler(ctrl_handler, TRUE);

    /* Initialise background process list */
    memset(bg_procs, 0, sizeof(bg_procs));
    bg_count = 0;

    print_banner();

    while (1) {
        /* Silently reap any finished background processes */
        reap_processes();

        print_prompt();

        if (!fgets(line, sizeof(line), stdin)) {
            /* EOF (e.g. piped input finished) */
            printf("\n");
            break;
        }

        /* Strip trailing newline / CR */
        {
            int len = (int)strlen(line);
            while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r'))
                line[--len] = '\0';
        }

        if (parse_command(line, &cmd) < 0) continue;

        if (is_builtin(&cmd)) {
            int should_exit = run_builtin(&cmd);
            free_parsed_cmd(&cmd);
            if (should_exit) break;
        } else {
            run_process(&cmd);
            free_parsed_cmd(&cmd);
        }
    }

    kill_all_processes();
    return 0;
}
