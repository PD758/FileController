/*
*
* github.com/PD758
* All rights reserved. 2025.
*
*/
#include <stdio.h>
#include <stdarg.h>

#include "c_common.h"

void initCommonControls(INITCOMMONCONTROLSEX* iccex) {
    iccex->dwSize = sizeof(INITCOMMONCONTROLSEX);
    iccex->dwICC = ICC_LISTVIEW_CLASSES | ICC_PROGRESS_CLASS;
    InitCommonControlsEx(iccex);
}

void FLog(const char* s) {
    puts(s);
    fflush(stdout);
}

void FLogf(const char* format, ...) {
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);

    fflush(stdout);
}

void WLog(const wchar_t* s) {
    wprintf(L"%s\n", s);
    fflush(stdout);
}
void WLogf(const wchar_t* format, ...) {
    va_list args;
    va_start(args, format);
    vwprintf(format, args);
    va_end(args);

    fflush(stdout);
}
