/*
 * globals.c — Single definition point for all shared global variables.
 */
#include "shell.h"

BgProcess bg_procs[MAX_BG_PROCS];
int       bg_count = 0;
HANDLE    g_fg_process = INVALID_HANDLE_VALUE;
