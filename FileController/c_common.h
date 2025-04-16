/*
*
* github.com/PD758
* All rights reserved. 2025.
*
*/
#pragma once

#include <windows.h>
#include <commctrl.h>
#include <fltuser.h>

#include "shared.h"

/* Constants definitions */

#define FCONTROLLER_PORT L"\\FileControllerPort"

#if _DEBUG
#define LOG(...) {puts(__VA_ARGS__);fflush(stdout);}
#define LOG_BYTES(pointer, size)                         \
    do {                                                 \
        unsigned char* _ptr = (unsigned char*)(pointer); \
        for (size_t i = 0; i < (size); ++i) {            \
            printf("%02X ", _ptr[i]);                    \
        }                                                \
        printf("\n");                                    \
    } while (0)

#else

#define LOG(...) ((void)0)
#define LOG_BYTES(pointer, size) ((void)0)

#endif


/* Structs definitions */


#define REQUEST_DIALOG_TIMEOUT 15 // seconds


typedef struct _Settings {
    UINT timeoutDuration;
    ULONG defaultAction;
} Settings, *PSettings;

typedef struct _UNICODE_STRING {
    USHORT Length;        // Текущая длина строки в байтах (без учета завершающего \0)
    USHORT MaximumLength; // Выделенный размер буфера в байтах
    PWSTR  Buffer;        // Указатель на буфер строки (широкие символы)
} UNICODE_STRING, * PUNICODE_STRING;

/* Functions declarations */

#ifdef __cplusplus
extern "C" {
#endif

void initCommonControls(INITCOMMONCONTROLSEX* iccex);

void FLog(const char* string);
void FLogf(const char* format, ...);
void WLog(const wchar_t* string);
void WLogf(const wchar_t* format, ...);

#ifdef __cplusplus
}
#endif
