#include "parser.h"

static int is_quote_escaped(const char *start, const char *p)
{
    int count = 0;
    const char *q = p;
    while (q > start && q[-1] == '\\') {
        count++;
        q--;
    }
    return (count % 2) != 0;
}

static void unescape_quoted_token(char *s, char quote)
{
    char *r = s;
    char *w = s;

    while (*r) {
        if (*r == '\\' && (r[1] == quote || r[1] == '\\')) {
            r++;
        }
        *w++ = *r++;
    }
    *w = '\0';
}

int parse_command(const char *line, ParsedCmd *cmd)
{
    static char buf[MAX_CMD_LEN];
    int   i, argc = 0;
    int   in_quote = 0;
    char  quote = '\0';
    char *p;
    int   end;

    memset(cmd, 0, sizeof(ParsedCmd));

    /* Copy so we can mutate */
    strncpy(buf, line, MAX_CMD_LEN - 1);
    buf[MAX_CMD_LEN - 1] = '\0';

    end = (int)strlen(buf) - 1;
    while (end >= 0 && (buf[end] == ' ' || buf[end] == '\t')) {
        buf[end--] = '\0';
    }

    for (i = 0; i <= end; i++) {
        if ((buf[i] == '"' || buf[i] == '\'') && !is_quote_escaped(buf, &buf[i])) {
            if (!in_quote) {
                in_quote = 1;
                quote = buf[i];
            } else if (quote == buf[i]) {
                in_quote = 0;
                quote = '\0';
            }
        }
    }

    if (!in_quote && end >= 0 && buf[end] == '&') {
        cmd->is_background = 1;
        buf[end] = '\0';
        while (--end >= 0 && (buf[end] == ' ' || buf[end] == '\t')) {
            buf[end] = '\0';
        }
    } else if (in_quote) {
        fprintf(stderr, "%s: parse: unmatched quote\n", SHELL_NAME);
        return -1;
    }

    p = buf;

    while (*p && argc < MAX_ARGS - 1) {
        /* Skip whitespace */
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;

        /* quoted token */
        if (*p == '"' || *p == '\'') {
            char quote = *p++;
            cmd->argv[argc++] = p;
            while (*p && (*p != quote || is_quote_escaped(buf, p))) p++;
            if (!*p) {
                fprintf(stderr, "%s: parse: unmatched quote\n", SHELL_NAME);
                return -1;
            }
            *p++ = '\0';
            unescape_quoted_token(cmd->argv[argc - 1], quote);
        } else {
            cmd->argv[argc++] = p;
            while (*p && *p != ' ' && *p != '\t') p++;
            if (*p) *p++ = '\0';
        }
    }

    if (argc == 0) return -1;
    if (*p) {
        fprintf(stderr, "%s: parse: too many arguments (max %d)\n",
                SHELL_NAME, MAX_ARGS - 1);
        return -1;
    }

    /* Duplicate argv strings so caller can free independently */
    for (i = 0; i < argc; i++) {
        cmd->argv[i] = _strdup(cmd->argv[i]);
        if (!cmd->argv[i]) {
            fprintf(stderr, "%s: parse: out of memory\n", SHELL_NAME);
            cmd->argc = i;
            free_parsed_cmd(cmd);
            return -1;
        }
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
