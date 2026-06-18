#include "parser.h"

int parse_command(const char *line, ParsedCmd *cmd)
{
    static char buf[MAX_CMD_LEN];
    int   i, argc = 0;
    char *p;

    memset(cmd, 0, sizeof(ParsedCmd));

    /* Copy so we can mutate */
    strncpy(buf, line, MAX_CMD_LEN - 1);
    buf[MAX_CMD_LEN - 1] = '\0';

    p = buf;

    while (*p && argc < MAX_ARGS - 1) {
        /* Skip whitespace */
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;

        /* quoted token */
        if (*p == '"' || *p == '\'') {
            char quote = *p++;
            cmd->argv[argc++] = p;
            while (*p && *p != quote) p++;
            if (*p) *p++ = '\0';
        } else {
            cmd->argv[argc++] = p;
            while (*p && *p != ' ' && *p != '\t') p++;
            if (*p) *p++ = '\0';
        }
    }

    if (argc == 0) return -1;

    /* Detect trailing background marker */
    if (argc > 0 && strcmp(cmd->argv[argc - 1], "&") == 0) {
        cmd->is_background = 1;
        argc--;
    }

    /* Duplicate argv strings so caller can free independently */
    for (i = 0; i < argc; i++) {
        cmd->argv[i] = _strdup(cmd->argv[i]);
    }
    cmd->argc = argc;
    cmd->argv[argc] = NULL;

    if (argc == 0) return -1;
    return 0;
}

void free_parsed_cmd(ParsedCmd *cmd)
{
    int i;
    for (i = 0; i < cmd->argc; i++) {
        free(cmd->argv[i]);
        cmd->argv[i] = NULL;
    }
    cmd->argc = 0;
}
