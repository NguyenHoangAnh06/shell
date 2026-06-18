#include "shell_utils.h"

/*
 * shell_perror — centralised Win32 error reporter.
 *
 * Rationale:
 *   Scattered fprintf(stderr, "... error %lu", GetLastError()) calls are
 *   hard to maintain and produce inconsistent messages.  This wrapper
 *   formats the Win32 error code into a human-readable string using
 *   FormatMessageA so the user sees something like:
 *
 *     myShell: CreateProcess: The system cannot find the file specified. (error 2)
 *
 *   instead of a raw numeric code.
 */
void shell_perror(const char *context)
{
    DWORD  err = GetLastError();
    char   msg[512];
    DWORD  len;

    len = FormatMessageA(
            FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
            NULL,
            err,
            MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
            msg,
            sizeof(msg) - 1,
            NULL);

    /* Strip trailing newline that FormatMessageA often appends */
    while (len > 0 && (msg[len-1] == '\n' || msg[len-1] == '\r'))
        msg[--len] = '\0';

    if (len == 0)
        snprintf(msg, sizeof(msg), "Unknown error");

    fprintf(stderr, "%s: %s: %s (error %lu)\n",
            SHELL_NAME, context, msg, (unsigned long)err);
}
