#pragma once
#include <cstdarg>

// Lightweight file-based logging used across hooks.
void AppendLog(const char* msg);
void AppendLogFmt(const char* fmt, ...);
