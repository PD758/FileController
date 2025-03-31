/*
*
* github.com/PD758
* All rights reserved. 2025.
*
*/
#pragma once

#include "c_common.h"

#ifdef __cplusplus
extern "C" {
#endif


BOOL InitializeDriver();
void CleanupDriver();
BOOL SendMessageToDriver(ULONG messageType, const wchar_t* data);

HANDLE GetPort();

#ifdef __cplusplus
}
#endif
