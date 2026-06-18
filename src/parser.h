#ifndef PARSER_H
#define PARSER_H

#include "shell.h"

/*
 * parse_command:
 *   Tokenises 'line' into cmd->argv / cmd->argc.
 *   Sets cmd->is_background if a trailing '&' is found.
 *   Handles single-quoted and double-quoted tokens.
 *   Returns 0 on success, -1 if the line is empty / whitespace-only.
 */
int parse_command(const char *line, ParsedCmd *cmd);

/* Free dynamic token strings allocated by parse_command */
void free_parsed_cmd(ParsedCmd *cmd);

#endif /* PARSER_H */
