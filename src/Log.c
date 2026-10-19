/* // * Friday Night Funkin´ c engine (c) 2026 nordjkpl */
#include "Log.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <unistd.h>
// * Log Direto no Cmd Só, OUTD day 18 10/2026

static void RawWrite(int fd, const char* s, size_t n) {
    while(n > 0) {
        long w = (long)write(fd, s, n);
        if(w <= 0) break;
        s += (size_t)w;
        n -= (size_t)w;
    }
}

// 1 syscall só: msg + \n no mesmo buffer
static void Emit(int fd, const char* msg) {
    if(msg == NULL) { RawWrite(fd, "\n", 1); return; }
    size_t len = strlen(msg);
    // fast path: cabe no stack = 1 write
    if(len + 1 < 512) {
        char tmp[512];
        memcpy(tmp, msg, len);
        tmp[len] = '\n';
        RawWrite(fd, tmp, len + 1);
    } else {
        // fallback raro: msg gigante >512
        RawWrite(fd, msg, len);
        RawWrite(fd, "\n", 1);
    }
}

static void EmitF(int fd, int fatal, const char* fmt, va_list ap) {
    char buf[512];
    // deixa 1 byte pro \n + 1 pro \0
    int len = vsnprintf(buf, sizeof(buf) - 1, fmt, ap);
    if(len < 0) len = 0;
    if(len > (int)sizeof(buf) - 2) len = (int)sizeof(buf) - 2;
    buf[len] = '\n';
    // 1 syscall só, sem chamar Emit() e sem strlen de novo
    RawWrite(fd, buf, (size_t)len + 1);
    if(fatal) exit(1);
}

void Log_Info(const char* msg)  { Emit(STDOUT_FILENO, msg); } // 1 em vez de fileno(stdout)
void Log_Error(const char* msg) { Emit(STDERR_FILENO, msg); } // 2 em vez de fileno(stderr)
void Log_Fatal(const char* msg) { Emit(STDERR_FILENO, msg); exit(1); }

void Log_ErrorF(const char* fmt, ...) {
    va_list ap; va_start(ap, fmt);
    EmitF(STDERR_FILENO, 0, fmt, ap);
    va_end(ap);
}
void Log_FatalF(const char* fmt, ...) {
    va_list ap; va_start(ap, fmt);
    EmitF(STDERR_FILENO, 1, fmt, ap);
    va_end(ap);
}
