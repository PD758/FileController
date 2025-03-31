/*
*
* github.com/PD758
* All rights reserved. 2025.
*
*/
#define _AMD64_

//#include <ntdef.h>

#include <windows.h>
#include <fltuser.h>
//#include <commctrl.h>
//#include <shlobj.h>
//#include <shlwapi.h>


#include "c_common.h"
#include "c_specific.h"

static HANDLE g_Port = INVALID_HANDLE_VALUE;

BOOL InitializeDriver() {
    HRESULT hr = FilterConnectCommunicationPort(
        FCONTROLLER_PORT,
        0,
        NULL,
        0,
        NULL,
        &g_Port
    );

    if (FAILED(hr)) {
        g_Port = INVALID_HANDLE_VALUE; // Устанавливаем в INVALID_HANDLE_VALUE при ошибке
    }
    return (g_Port != INVALID_HANDLE_VALUE);
}

void CleanupDriver() {
    if (g_Port != INVALID_HANDLE_VALUE) {
        CloseHandle(g_Port);
        g_Port = INVALID_HANDLE_VALUE;
    }
}

BOOL SendMessageToDriver(ULONG messageType, const wchar_t* data) {
    if (g_Port == INVALID_HANDLE_VALUE) {
        FLog("sendMessageToDriver failed: port handle closed");
        return FALSE;
    }

    // Message buffer
    ULONG messageSize = FLT_INC_MSG_STRUCT_SIZE + sizeof(UNICODE_STRING) + wcslen(data) * sizeof(WCHAR);

    //std::unique_ptr<UCHAR[]> message(new UCHAR[messageSize]); // Use unique_ptr
    UCHAR* message = (UCHAR*)calloc(messageSize, sizeof(UCHAR));

    if (!message) {
        FLog("sendMessageToDriver failed: message isnt");
        return FALSE;
    }

    // Заполняем сообщение
    (*(PFILTER_INCOMING_MESSAGE_STRUCT)message).Header.ReplyLength = FLT_INC_RPL_STRUCT_SIZE;
    (*(PFILTER_INCOMING_MESSAGE_STRUCT)message).messageType = messageType;

    PUNICODE_STRING unicodeString = (PUNICODE_STRING)(message + FLT_INC_MSG_STRUCT_SIZE);
    unicodeString->Length = (USHORT)(wcslen(data) * sizeof(WCHAR));
    unicodeString->MaximumLength = (USHORT)(wcslen(data) * sizeof(WCHAR));
    unicodeString->Buffer = (PWCH)(message + FLT_INC_MSG_STRUCT_SIZE + sizeof(UNICODE_STRING));
    memcpy(unicodeString->Buffer, data, wcslen(data) * sizeof(WCHAR));

    // Отправляем сообщение
    ULONG replySize;
    FILTER_INCOMING_REPLY_STRUCT reply;

    HRESULT hr = FilterSendMessage(
        g_Port,
        message,
        messageSize,
        &reply,
        FLT_INC_RPL_STRUCT_SIZE,
        &replySize
    );

    free(message);
    
    FLogf("sendMessageToDriver: got HR=%d, reply.responseType=%d\n", (long)hr, reply.responseType);

    return SUCCEEDED(hr) && (reply.responseType == 1);
}

HANDLE GetPort() {
    return g_Port;
}
