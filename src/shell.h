#ifndef SHELL_H
#define SHELL_H

/* Expose MSVCRT specific POSIX functions (_strdup, _stricmp) under -std=c99 */
#if defined(__STRICT_ANSI__)
#undef __STRICT_ANSI__
#endif

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#if !defined(_MSC_VER)
char *_strdup(const char *s);
int _stricmp(const char *a, const char *b);
#endif

/* ── Limits ─────────────────────────────────────────── */
#define MAX_CMD_LEN     1024
#define MAX_ARGS        64
#define MAX_BG_PROCS    64
#define SHELL_NAME      "myShell"

/* ── Process status ──────────────────────────────────── */
typedef enum {
    PROC_RUNNING  = 0,
    PROC_STOPPED  = 1,
    PROC_DONE     = 2
} ProcStatus;

/* ── Background process entry ────────────────────────── */
typedef struct {
    DWORD      pid;
    HANDLE     hProcess;
    char       cmd[MAX_CMD_LEN];
    ProcStatus status;
    int        active;   /* 1 = slot in use */
} BgProcess;

/* ── Parsed command ──────────────────────────────────── */
typedef struct {
    char  *argv[MAX_ARGS];
    int    argc;
    int    is_background;  /* trailing & detected */
} ParsedCmd;

/* ── Globals (defined in globals.c) ─────────────────────── */
extern BgProcess bg_procs[MAX_BG_PROCS];
extern int       bg_count;
extern HANDLE    g_fg_process;   /* handle to current foreground child */
extern volatile DWORD g_fg_pid;   /* process-group id for CTRL+C routing */

#endif /* SHELL_H */
