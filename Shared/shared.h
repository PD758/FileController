/*
*
* github.com/PD758
* All rights reserved. 2025.
*
*/
#pragma once

// Structures for I/O Control
__declspec(align(8))
typedef struct _ACCESS_REQUEST {
    ULONG RequestType;
    WCHAR FileName[256];
    WCHAR ProgramName[256];
} ACCESS_REQUEST, * PACCESS_REQUEST;

__declspec(align(8))
typedef struct _FLT_AC_STRUCT {
    FILTER_MESSAGE_HEADER Header;
    ACCESS_REQUEST Data;
} FILTER_MESSAGE_ACCESS_REQUEST, * PFILTER_MESSAGE_ACCESS_REQUEST;

__declspec(align(8))
typedef struct _ACCESS_REPLY {
    ULONG ReplyType;
} ACCESS_REPLY, * PACCESS_REPLY;

__declspec(align(8))
typedef struct _FLT_RPL_STRUCT {
    FILTER_REPLY_HEADER Header;
    ACCESS_REPLY Data;
} FILTER_REPLY_ACCESS_RESPONSE, * PFILTER_REPLY_ACCESS_RESPONSE;

__declspec(align(8))
typedef struct _FLT_INCOMING_MSG_STRUCT {
    FILTER_MESSAGE_HEADER Header;
    ULONG messageType;
} FILTER_INCOMING_MESSAGE_STRUCT, * PFILTER_INCOMING_MESSAGE_STRUCT;

__declspec(align(8))
typedef struct _FLT_INCOMING_MSG_STRUCT_RETURN {
    FILTER_REPLY_HEADER Header;
    ULONG responseType;
} FILTER_INCOMING_REPLY_STRUCT, * PFILTER_INCOMING_REPLY_STRUCT;

#define FLT_MSG_ACCESS_REQUEST_SIZE sizeof(FILTER_MESSAGE_ACCESS_REQUEST)
#define FLT_RPL_ACCESS_RESPONSE_SIZE sizeof(FILTER_REPLY_ACCESS_RESPONSE)
#define FLT_INC_MSG_STRUCT_SIZE sizeof(FILTER_INCOMING_MESSAGE_STRUCT)
#define FLT_INC_RPL_STRUCT_SIZE sizeof(FILTER_INCOMING_REPLY_STRUCT)

#define MESSAGE_TYPE_ADD_FILE       0x00001002
#define MESSAGE_TYPE_REMOVE_FILE    0x00002003
#define MESSAGE_TYPE_ADD_PROGRAM    0x00003004
#define MESSAGE_TYPE_REMOVE_PROGRAM 0x00004005

#define RESPONSE_TYPE_ACCESS_GRANTED 0x00001006
#define RESPONSE_TYPE_ACCESS_DENIED  0x00002007
#define RESPONSE_TYPE_ADD_TRUSTED    0x00003008
#define RESPONSE_TYPE_BLACKLIST      0x00004009

// Constants for filesystem I/O Operation Types
#define FILESYSTEM_READ   0x00000100
#define FILESYSTEM_WRITE  0x00000200
#define FILESYSTEM_DELETE 0x00000400
