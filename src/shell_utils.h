#ifndef SHELL_UTILS_H
#define SHELL_UTILS_H

#include "shell.h"

/*
 * shell_perror:
 *   Unified error reporter. Prints:
 *     "myShell: <context>: <Windows error message> (error <code>)"
 *   to stderr. Call after any Win32 API failure.
 *
 *   Example:
 *     if (!CreateProcess(...)) { shell_perror("CreateProcess"); return; }
 */
void shell_perror(const char *context);

#endif /* SHELL_UTILS_H */
