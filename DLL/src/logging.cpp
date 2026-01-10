#include "pch.h"
#include <windows.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "logging.h"

void AppendLog(const char* msg)
{
    HANDLE h = CreateFileA("D:\\Project\\overlay_log.txt", FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return;
    DWORD written = 0;
    const size_t len = strlen(msg);
    if (len > 0)
        WriteFile(h, msg, static_cast<DWORD>(len), &written, nullptr);
    CloseHandle(h);
}

void AppendLogFmt(const char* fmt, ...)
{
    char buf[512]{};
    va_list args;
    va_start(args, fmt);
    _vsnprintf_s(buf, sizeof(buf), _TRUNCATE, fmt, args);
    va_end(args);
    AppendLog(buf);
}
