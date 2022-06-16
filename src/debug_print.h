#pragma once

#include <stdio.h>
#include <stdarg.h>

#ifndef ENABLE_DEBUG_PRINT
#define ENABLE_DEBUG_PRINT  0
#endif

static void debug_print(char *fmt, ...)
{
#if ENABLE_DEBUG_PRINT
   va_list arg_ptr;
   va_start(arg_ptr, fmt);
   vprintf(fmt, arg_ptr);
   va_end(arg_ptr);
#endif
}

#define DEBUG_FMT  "%s:%s:%d: "
#define DEBUG_SRC  __FILE__, __PRETTY_FUNCTION__, __LINE__
#define DEBUG_PRINT(fmt, ...)  debug_print(DEBUG_FMT fmt, DEBUG_SRC, __VA_ARGS__)
#define DPRINT(fmt, ...)  debug_print(fmt, __VA_ARGS__)
