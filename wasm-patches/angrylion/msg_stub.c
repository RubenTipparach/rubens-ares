// Logging stub for WASM — routes to emscripten console
#include "msg.h"
#include <stdio.h>
#include <stdarg.h>

void msg_error(const char* err, ...)
{
    va_list args;
    va_start(args, err);
    fprintf(stderr, "[angrylion ERROR] ");
    vfprintf(stderr, err, args);
    fprintf(stderr, "\n");
    va_end(args);
}

void msg_warning(const char* err, ...)
{
    va_list args;
    va_start(args, err);
    fprintf(stderr, "[angrylion WARN] ");
    vfprintf(stderr, err, args);
    fprintf(stderr, "\n");
    va_end(args);
}

void msg_debug(const char* err, ...)
{
    // silent in release builds
    (void)err;
}
