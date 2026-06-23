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

wchar_t *utf8_to_wide(const char *text);
char *wide_to_utf8(const wchar_t *text);
void shell_print_wide(FILE *stream, const wchar_t *text);

#endif /* SHELL_UTILS_H */
