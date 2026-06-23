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
 *   - If a foreground process is running, terminate it and leave the shell
 *     alive. CTRL_BREAK_EVENT is not sufficient because programs such as the
 *     Windows ping command treat it as "print statistics and continue".
 *   - Otherwise, ignore (do not kill the shell).
 */

#include "shell.h"
#include "parser.h"
#include "process.h"
#include "process_list.h"
#include "builtins.h"
#include "shell_utils.h"
#include "line_editor.h"


/* ─────────────────────────────────────────────────────── */
static BOOL WINAPI ctrl_handler(DWORD ctrl_type)
{
    if (ctrl_type == CTRL_C_EVENT || ctrl_type == CTRL_BREAK_EVENT) {
        DWORD pid = g_fg_pid;
        if (pid != 0) {
            HANDLE process = g_fg_process;
            /*
             * Use forced termination here so CTRL+C has predictable shell
             * semantics. In particular, Windows ping does not exit on
             * CTRL_BREAK_EVENT.
             */
            if (process == INVALID_HANDLE_VALUE ||
                !TerminateProcess(process, 130)) {
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
    DWORD needed = GetCurrentDirectoryW(0, NULL);
    wchar_t *cwd = NULL;

    if (needed > 0)
        cwd = (wchar_t *)malloc((size_t)needed * sizeof(wchar_t));

    if (cwd && GetCurrentDirectoryW(needed, cwd)) {
        printf("\n[");
        shell_print_wide(stdout, cwd);
        printf("]> ");
    } else {
        printf("\n%s> ", SHELL_NAME);
    }
    free(cwd);
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
    HANDLE stdin_handle = GetStdHandle(STD_INPUT_HANDLE);
    DWORD stdin_mode;
    int interactive = GetConsoleMode(stdin_handle, &stdin_mode) != 0;

    /* Register CTRL+C / CTRL+BREAK handler */
    SetConsoleCtrlHandler(ctrl_handler, TRUE);
    SetConsoleCP(CP_UTF8);
    SetConsoleOutputCP(CP_UTF8);

    /* Initialise background process list */
    memset(bg_procs, 0, sizeof(bg_procs));
    bg_count = 0;

    print_banner();

    while (1) {
        /* Silently reap any finished background processes */
        reap_processes();

        print_prompt();

        if (interactive) {
            int read_result = read_console_line(line, sizeof(line));
            if (read_result == 0) {
                printf("\n");
                break;
            }
            if (read_result == 2) continue;
            if (read_result < 0) {
                shell_perror("console input");
                break;
            }
        } else {
            if (!fgets(line, sizeof(line), stdin)) {
                /* EOF (e.g. piped input finished) */
                printf("\n");
                break;
            }

            if (!strchr(line, '\n') && !feof(stdin)) {
                int ch;
                while ((ch = fgetc(stdin)) != '\n' && ch != EOF) {
                    /* discard the remainder of the overlong command */
                }
                fprintf(stderr, "%s: command too long (max %d bytes)\n",
                        SHELL_NAME, MAX_CMD_LEN - 1);
                continue;
            }

            /* Strip trailing newline / CR */
            {
                int len = (int)strlen(line);
                while (len > 0 &&
                       (line[len - 1] == '\n' || line[len - 1] == '\r'))
                    line[--len] = '\0';
            }
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
