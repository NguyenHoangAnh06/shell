#ifndef LINE_EDITOR_H
#define LINE_EDITOR_H

#include "shell.h"

/*
 * Read one interactive console line with completion and prefix history.
 * Returns 1 for a line, 0 for EOF, 2 when CTRL+C cancels the current line,
 * and -1 on error. Redirected input should continue to use fgets().
 */
int read_console_line(char *out, size_t outsize);

#endif /* LINE_EDITOR_H */
