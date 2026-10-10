#include "Log.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <unistd.h>

// write() direto no fileno: sem buffer stdio, sem locale, sem lock.
// Vale no Windows (MinGW mapeia pro CRT) e no resto.
static void RawWrite(int fd, const char* s, size_t n) {
    while(n > 0) {
        long w = (long)write(fd, s, n);
        if(w <= 0)
            break;
        s += w;
        n -= (size_t)w;
    }
}

static void Emit(int fd, const char* msg) {
    if(msg != NULL)
        RawWrite(fd, msg, strlen(msg));
    RawWrite(fd, "\n", 1);
}

void Log_Info(const char* msg) {
    Emit(fileno(stdout), msg);
}

void Log_Error(const char* msg) {
    Emit(fileno(stderr), msg);
}

void Log_Fatal(const char* msg) {
    Emit(fileno(stderr), msg);
    exit(1);
}

static void EmitF(int fd, int fatal, const char* fmt, va_list ap) {
    char buf[512];
    vsnprintf(buf, sizeof(buf), fmt, ap);
    Emit(fd, buf);
    if(fatal)
        exit(1);
}

void Log_ErrorF(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    EmitF(fileno(stderr), 0, fmt, ap);
    va_end(ap);
}

void Log_FatalF(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    EmitF(fileno(stderr), 1, fmt, ap);
    va_end(ap);
}
