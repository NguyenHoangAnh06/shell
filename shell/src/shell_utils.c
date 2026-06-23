#include "shell_utils.h"

wchar_t *utf8_to_wide(const char *text)
{
    int needed;
    wchar_t *result;

    if (!text) return NULL;
    needed = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                 text, -1, NULL, 0);
    if (needed <= 0) return NULL;

    result = (wchar_t *)malloc((size_t)needed * sizeof(wchar_t));
    if (!result) {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return NULL;
    }
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                             text, -1, result, needed)) {
        free(result);
        return NULL;
    }
    return result;
}

char *wide_to_utf8(const wchar_t *text)
{
    int needed;
    char *result;

    if (!text) return NULL;
    needed = WideCharToMultiByte(CP_UTF8, 0, text, -1,
                                 NULL, 0, NULL, NULL);
    if (needed <= 0) return NULL;

    result = (char *)malloc((size_t)needed);
    if (!result) {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return NULL;
    }
    if (!WideCharToMultiByte(CP_UTF8, 0, text, -1,
                             result, needed, NULL, NULL)) {
        free(result);
        return NULL;
    }
    return result;
}

void shell_print_wide(FILE *stream, const wchar_t *text)
{
    char *utf8 = wide_to_utf8(text);
    if (!utf8) {
        fputs("<encoding error>", stream);
        return;
    }
    fputs(utf8, stream);
    free(utf8);
}

void shell_perror(const char *context)
{
    DWORD err = GetLastError();
    wchar_t msg[512];
    DWORD len;
    char *utf8_msg;

    len = FormatMessageW(
        FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL,
        err,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        msg,
        (DWORD)(sizeof(msg) / sizeof(msg[0])),
        NULL);

    while (len > 0 && (msg[len - 1] == L'\n' || msg[len - 1] == L'\r'))
        msg[--len] = L'\0';

    if (len == 0)
        wcscpy(msg, L"Unknown error");

    utf8_msg = wide_to_utf8(msg);
    fprintf(stderr, "%s: %s: %s (error %lu)\n",
            SHELL_NAME,
            context,
            utf8_msg ? utf8_msg : "Unknown error",
            (unsigned long)err);
    free(utf8_msg);
}
