/** @file DebugLog.c
 *
 *  A simple file logger for debugging purposes.
 *
 *  Copyright (c) 2023, Michael Fitzmayer. All rights reserved.
 *  SPDX-License-Identifier: MIT
 *
 **/

#include <stdarg.h>
#include <stdio.h>
#include "DebugLog.h"

#if defined ENABLE_DEBUG_LOG
void DebugLog(const char* filename, const char* format, ...)
{
    FILE*   log = NULL;
    va_list varg;

    log = fopen(filename, "a");

    if (log)
    {
        va_start(varg, format);
        vfprintf(log, format, varg);
        va_end(varg);

        fclose(log);
    }
}
#else

void DebugLog(const char* filename, const char* format, ...) {}

#endif /* defined ENABLE_DEBUG_LOG */