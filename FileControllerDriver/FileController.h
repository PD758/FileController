/*
* 
* github.com/PD758
* All rights reserved. 2025.
* 
*/
#ifndef FILECONTROLLER_H
#define FILECONTROLLER_H

#include <fltKernel.h>
#include <dontuse.h>
#include <suppress.h>
#include <ntddscsi.h>
#include <ntddstor.h>
#include <ntstrsafe.h>
#include <ntifs.h>
//#include <ntddk.h>
//#include <wdm.h>

#include "shared.h"

#pragma prefast(disable:__WARNING_ENCODE_MEMBER_FUNCTION_POINTER, "Not valid for kernel mode drivers")

#pragma warning(disable: 4100) // unused formal parameter

// Debugging

#define SECONDS_TO_100NS(seconds) ((seconds) * 10000000LL) // 1 second = 10^7 hundred nanoseconds

#define PTDBG_TRACE_ROUTINES     0x00000001
#define PTDBG_TRACE_OPERATION_STATUS 0x00000002
extern ULONG gTraceFlags;
#define PT_DBG_PRINT( _dbgLevel, _string )          \
    (FlagOn(gTraceFlags,(_dbgLevel)) ?              \
        DbgPrint _string :                          \
        ((int)0))
#define PT_DBG_PRINTF(_dbgLevel, _format, ...) \
    (FlagOn(gTraceFlags,(_dbgLevel)) ? DbgPrint(_format, __VA_ARGS__) : 0)
#define PRINTF(_format, ...) PT_DBG_PRINTF(PTDBG_TRACE_ROUTINES, _format, __VA_ARGS__)
#define DPRINT(_string) PT_DBG_PRINT(PTDBG_TRACE_ROUTINES, (_string))

// print pointer data in byte-hex format
#define LOG_BYTES(pointer, size) do { \
        for (SIZE_T i = 0; i < (size); ++i) {                          \
            PT_DBG_PRINTF(PTDBG_TRACE_ROUTINES, ("%02X "), ((PUCHAR)(pointer))[i]);   \
        }                                                              \
        DPRINT("\n");                                                  \
    } while (0)
#define _DISABLED(...) (0)

// Communication with User-Mode
#define FILECONTROLLER_PORT_NAME L"\\FileControllerPort"
extern PFLT_PORT gServerPort;
extern PFLT_PORT gClientPort;

// Data Structures
typedef struct _PROTECTED_FILE {
    UNICODE_STRING FileName;
    LIST_ENTRY ListEntry;
} PROTECTED_FILE, * PPROTECTED_FILE;

typedef struct _TRUSTED_PROGRAM {
    UNICODE_STRING ProgramName;
    LIST_ENTRY ListEntry;
} TRUSTED_PROGRAM, * PTRUSTED_PROGRAM;

// Global Lists
extern LIST_ENTRY g_ProtectedFilesList;
extern LIST_ENTRY g_TrustedProgramsList;
extern FAST_MUTEX g_ListLock;



#define ACCESS_REQUEST_MESSAGE_REPLY_TIMEOUT -SECONDS_TO_100NS(20) // do not define, if no timeout, else -(seconds*10^7)


#define DEFAULT_RESPONSE_TYPE RESPONSE_TYPE_ACCESS_DENIED // default response, used when request was not succedded

// Function Prototypes
DRIVER_INITIALIZE DriverEntry;
NTSTATUS DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath
);

NTSTATUS FileControllerUnload(
    _In_ FLT_FILTER_UNLOAD_FLAGS Flags
);

NTSTATUS FileControllerInstanceSetup(
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _In_ FLT_INSTANCE_SETUP_FLAGS Flags,
    _In_ DEVICE_TYPE VolumeDeviceType,
    _In_ FLT_FILESYSTEM_TYPE VolumeFilesystemType
);

VOID FileControllerInstanceTeardownStart(
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _In_ FLT_INSTANCE_TEARDOWN_FLAGS Flags
);

VOID FileControllerInstanceTeardownComplete(
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _In_ FLT_INSTANCE_TEARDOWN_FLAGS Flags
);

NTSTATUS FileControllerInstanceQueryTeardown(
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _In_ FLT_INSTANCE_QUERY_TEARDOWN_FLAGS Flags
);

FLT_PREOP_CALLBACK_STATUS FileControllerPreOperation(
    _Inout_ PFLT_CALLBACK_DATA Data,
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _Flt_CompletionContext_Outptr_ PVOID* CompletionContext
);

FLT_POSTOP_CALLBACK_STATUS FileControllerPostOperation(
    _Inout_ PFLT_CALLBACK_DATA Data,
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _In_opt_ PVOID CompletionContext,
    _In_ FLT_POST_OPERATION_FLAGS Flags
);

NTSTATUS FileControllerConnect(
    _In_ PFLT_PORT ClientPort,
    _In_ PVOID ServerPortCookie,
    _In_reads_bytes_(SizeOfContext) PVOID ConnectionContext,
    _In_ ULONG SizeOfContext,
    _Outptr_result_maybenull_ PVOID* ConnectionCookie
);

VOID FileControllerDisconnect(
    _In_opt_ PVOID ConnectionCookie
);

NTSTATUS FileControllerMessage(
    _In_ PVOID ConnectionCookie,
    _In_reads_bytes_opt_(InputBufferSize) PVOID InputBuffer,
    _In_ ULONG InputBufferSize,
    _Out_writes_bytes_to_opt_(OutputBufferSize, *ReturnOutputBufferSize) PVOID OutputBuffer,
    _In_ ULONG OutputBufferSize,
    _Out_ PULONG ReturnOutputBufferSize
);

BOOLEAN IsFileProtected(
    _In_ PUNICODE_STRING FileName
);

BOOLEAN IsProgramTrusted(
    _In_ PUNICODE_STRING ProgramName
);

NTSTATUS AskUserForPermission(
    _In_ ULONG RequestType,
    _In_ PUNICODE_STRING FileName,
    _In_ PUNICODE_STRING ProgramName,
    _Out_ PACCESS_REPLY Reply
);

// for the IDE to work, because this function isn't in .h files
NTSYSCALLAPI
NTSTATUS
NTAPI
ZwQueryInformationProcess(
    HANDLE ProcessHandle,
    PROCESSINFOCLASS ProcessInformationClass,
    PVOID ProcessInformation,
    ULONG ProcessInformationLength,
    PULONG ReturnLength OPTIONAL
);

NTSTATUS GetProcessFullImagePath(_In_ PEPROCESS Process,
    _Out_writes_bytes_to_opt_(sizeof(UNICODE_STRING), 1) PUNICODE_STRING* FullImagePath);

NTSTATUS FileExists(PCUNICODE_STRING NtFilePath);
NTSTATUS OpenFileByNtPath(
    PFLT_FILTER Filter,
    PCUNICODE_STRING NtFilePath,
    PFLT_INSTANCE Instance,
    PHANDLE FileHandle,
    PFILE_OBJECT* FileObject
);
VOID CloseFileHandle(HANDLE FileHandle, PFILE_OBJECT FileObject);
VOID FlushAndPurgeFileCache(PFILE_OBJECT FileObject);
NTSTATUS FlushCacheForFile(
    PFLT_FILTER Filter,
    PFLT_INSTANCE Instance,
    PCUNICODE_STRING NtFilePath
);
NTSTATUS GetInstanceForVolume(
    PCUNICODE_STRING VolumeName,
    PFLT_INSTANCE* Instance
);
// Extract "\Volume\HarddiskDrive1" part from NT PATH
NTSTATUS ExtractPathVolume(PUNICODE_STRING path, PUNICODE_STRING volume);

BOOLEAN ContainsSubstringWcharArray(
    _In_ PCUNICODE_STRING StringToSearch,
    _In_reads_(SubstringLengthChars) PCWSTR Substring,
    _In_ USHORT SubstringLengthChars
);

#endif // FILECONTROLLER_H