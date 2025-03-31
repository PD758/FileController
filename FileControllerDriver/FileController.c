/*
* 
* github.com/PD758
* All rights reserved. 2025.
* 
*/
#include "FileController.h"

PFLT_FILTER gFilterHandle;
ULONG_PTR OperationStatusCtx = 1;

ULONG gTraceFlags = PTDBG_TRACE_ROUTINES + PTDBG_TRACE_OPERATION_STATUS;

// Коммуникация с пользовательским режимом
PFLT_PORT gServerPort = NULL;
PFLT_PORT gClientPort = NULL;

// Глобальные списки защищенных файлов и доверенных программ
LIST_ENTRY g_ProtectedFilesList;
LIST_ENTRY g_TrustedProgramsList;
FAST_MUTEX g_ListLock;


const FLT_OPERATION_REGISTRATION Callbacks[] = {
    { IRP_MJ_CREATE,              0, FileControllerPreOperation, FileControllerPostOperation },
    { IRP_MJ_READ,                0, FileControllerPreOperation, FileControllerPostOperation },
    { IRP_MJ_WRITE,               0, FileControllerPreOperation, FileControllerPostOperation },
    { IRP_MJ_SET_INFORMATION,     0, FileControllerPreOperation, FileControllerPostOperation },
    { IRP_MJ_OPERATION_END }
};

// Регистрация фильтра
const FLT_REGISTRATION FilterRegistration = {
    sizeof(FLT_REGISTRATION),           // Size
    FLT_REGISTRATION_VERSION,           // Version
    0,                                  // Flags
    NULL,                               // Context
    Callbacks,                          // Operation callbacks
    FileControllerUnload,                  // Unload
    FileControllerInstanceSetup,           // InstanceSetup
    FileControllerInstanceQueryTeardown,   // InstanceQueryTeardown
    FileControllerInstanceTeardownStart,   // InstanceTeardownStart
    FileControllerInstanceTeardownComplete,// InstanceTeardownComplete
    NULL,                               // GenerateFileName
    NULL,                               // GenerateDestinationFileName
    NULL                                // NormalizeNameComponent
};

NTSTATUS GetInstanceForVolume(
    PCUNICODE_STRING VolumeName,
    PFLT_INSTANCE* Instance
) {
    NTSTATUS Status;
    PFLT_VOLUME Volume = NULL;

    // get PFLT_VOLUME by volume name
    Status = FltGetVolumeFromName(gFilterHandle, VolumeName, &Volume);
    if (!NT_SUCCESS(Status)) {
        return Status;
    }

    // get PFLT_INSTANCE by PFLT_VOLUME
    Status = FltGetVolumeInstanceFromName(
        gFilterHandle,
        Volume,
        NULL,
        Instance
    );

    FltObjectDereference(Volume);

    return Status;
}
NTSTATUS GetProcessFullImagePath(_In_ PEPROCESS Process,
   _Out_writes_bytes_to_opt_(sizeof(UNICODE_STRING), 1) PUNICODE_STRING* FullImagePath) {
    NTSTATUS status;
    HANDLE processHandle;
    ULONG returnedLength;
    PVOID buffer;
    ULONG bufferSize = 512;

    status = ObOpenObjectByPointer(Process, OBJ_KERNEL_HANDLE, NULL, 0, *PsProcessType, KernelMode, &processHandle);
    if (!NT_SUCCESS(status)) return status;

    buffer = ExAllocatePool2(POOL_FLAG_NON_PAGED, bufferSize, 'proc');

    if (!buffer) {
        ZwClose(processHandle);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    status = ZwQueryInformationProcess(processHandle, ProcessImageFileName, buffer, bufferSize, &returnedLength);
    if (NT_SUCCESS(status)) {
        *FullImagePath = (PUNICODE_STRING)buffer;
    }
    else {
        ExFreePool(buffer);
    }

    ZwClose(processHandle);
    return status;
}

NTSTATUS FileExists(PCUNICODE_STRING NtFilePath) {
    OBJECT_ATTRIBUTES ObjectAttributes;
    FILE_NETWORK_OPEN_INFORMATION FileInfo;
    NTSTATUS Status;

    // init object attributes for file path
    InitializeObjectAttributes(&ObjectAttributes,
        (PUNICODE_STRING)NtFilePath,
        OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE,
        NULL,
        NULL);

    // get file attributes
    Status = ZwQueryFullAttributesFile(&ObjectAttributes, &FileInfo);
    return Status;
}

NTSTATUS OpenFileByNtPath(
    PFLT_FILTER Filter,
    PCUNICODE_STRING NtFilePath,
    PFLT_INSTANCE Instance,
    PHANDLE FileHandle,
    PFILE_OBJECT* FileObject
) {
    OBJECT_ATTRIBUTES ObjectAttributes;
    IO_STATUS_BLOCK IoStatus;
    NTSTATUS Status;

    Status = FileExists(NtFilePath);
    if (!NT_SUCCESS(Status)) {
        return Status;
    }

    InitializeObjectAttributes(&ObjectAttributes,
        (PUNICODE_STRING)NtFilePath,
        OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE,
        NULL,
        NULL);

    Status = FltCreateFile(
        Filter,
        Instance,
        FileHandle,
        FILE_READ_DATA | FILE_WRITE_DATA | SYNCHRONIZE,
        &ObjectAttributes,
        &IoStatus,
        NULL,
        FILE_ATTRIBUTE_NORMAL,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        FILE_OPEN,
        FILE_SYNCHRONOUS_IO_NONALERT,
        NULL,
        0,
        0
    );

    if (!NT_SUCCESS(Status)) {
        return Status;
    }

    Status = ObReferenceObjectByHandle(*FileHandle,
        0,
        *IoFileObjectType,
        KernelMode,
        (PVOID*)FileObject,
        NULL);

    return Status;
}
VOID CloseFileHandle(HANDLE FileHandle, PFILE_OBJECT FileObject) {
    if (FileObject) {
        ObDereferenceObject(FileObject);
    }
    if (FileHandle) {
        ZwClose(FileHandle);
    }
}
VOID FlushAndPurgeFileCache(PFILE_OBJECT FileObject) {
    if (FileObject->SectionObjectPointer) {
        // Drop cache to drive
        CcFlushCache(FileObject->SectionObjectPointer, NULL, 0, NULL);

        // Remove paged memory cache 
        CcPurgeCacheSection(FileObject->SectionObjectPointer, NULL, 0, FALSE);
    }
}
NTSTATUS FlushCacheForFile(
    PFLT_FILTER Filter,
    PFLT_INSTANCE Instance,
    PCUNICODE_STRING NtFilePath
) {
    HANDLE FileHandle = NULL;
    PFILE_OBJECT FileObject = NULL;
    NTSTATUS Status;

    Status = FileExists(NtFilePath);
    if (!NT_SUCCESS(Status)) {
        return Status;
    }

    Status = OpenFileByNtPath(Filter, NtFilePath, Instance, &FileHandle, &FileObject);
    if (!NT_SUCCESS(Status)) {
        return Status;
    }

    FlushAndPurgeFileCache(FileObject);

    CloseFileHandle(FileHandle, FileObject);

    return STATUS_SUCCESS;
}
NTSTATUS ExtractPathVolume(PUNICODE_STRING path, PUNICODE_STRING volume) {
    DPRINT("FileControllerDriver: extracting path\n");
    UINT16 backslash_counter = 0;
    volume->Length = 0;
    for (DWORD i = 0; (i < path->Length && i < volume->MaximumLength); i++) {
        if (path->Buffer[i] == L'\\')
            backslash_counter++;
        if (backslash_counter > 2)
            break;
        volume->Buffer[i] = path->Buffer[i];
        volume->Length++;
    }
    return STATUS_SUCCESS;
}
BOOLEAN ContainsSubstringWcharArray(
    _In_ PCUNICODE_STRING StringToSearch,
    _In_reads_(SubstringLengthChars) PCWSTR Substring,
    _In_ USHORT SubstringLengthChars
)
{
    if (!StringToSearch || !StringToSearch->Buffer || !Substring || SubstringLengthChars == 0) {
        DPRINT("FileControllerDriver: invalid IN_STRING args\n");
        return FALSE;
    }

    USHORT stringToSearchLengthChars = StringToSearch->Length / sizeof(WCHAR);

    if (SubstringLengthChars > stringToSearchLengthChars) {
        return FALSE;
    }

    if (stringToSearchLengthChars == 0 && SubstringLengthChars == 0) {
        return TRUE;
    }
    if (SubstringLengthChars == 0) {
        return TRUE;
    }

    for (USHORT i = 0; i <= (stringToSearchLengthChars - SubstringLengthChars); ++i)
    {
        if (RtlCompareMemory(Substring, &StringToSearch->Buffer[i], 
            SubstringLengthChars * sizeof(WCHAR)) == (SubstringLengthChars * sizeof(WCHAR))) {
            return TRUE;
        }
    }

    return FALSE;
}
// Точка входа драйвера
NTSTATUS DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath
)
{
    NTSTATUS status;
    PSECURITY_DESCRIPTOR sd;
    OBJECT_ATTRIBUTES oa;
    UNICODE_STRING portName;

    PT_DBG_PRINT(PTDBG_TRACE_ROUTINES, ("FileControllerDriver: DriverEntry\n"));

    InitializeListHead(&g_ProtectedFilesList);
    InitializeListHead(&g_TrustedProgramsList);
    ExInitializeFastMutex(&g_ListLock);

    status = FltRegisterFilter(DriverObject, &FilterRegistration, &gFilterHandle);
    if (!NT_SUCCESS(status)) {
        PT_DBG_PRINTF(PTDBG_TRACE_ROUTINES, ("FileControllerDriver: FltRegisterFilter failed with status 0x%x\n"), status);
        return status;
    }

    RtlInitUnicodeString(&portName, FILECONTROLLER_PORT_NAME);

    status = FltBuildDefaultSecurityDescriptor(&sd, FLT_PORT_ALL_ACCESS);
    if (!NT_SUCCESS(status)) {
        PT_DBG_PRINTF(PTDBG_TRACE_ROUTINES, ("FileControllerDriver: FltBuildDefaultSecurityDescriptor failed with status 0x%x\n"), status);
        FltUnregisterFilter(gFilterHandle);
        return status;
    }

    InitializeObjectAttributes(&oa, &portName, OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE, NULL, sd);

    status = FltCreateCommunicationPort(
        gFilterHandle,
        &gServerPort,
        &oa,
        NULL,
        FileControllerConnect,
        FileControllerDisconnect,
        FileControllerMessage,
        1
    );

    FltFreeSecurityDescriptor(sd);

    if (!NT_SUCCESS(status)) {
        PT_DBG_PRINTF(PTDBG_TRACE_ROUTINES, ("FileControllerDriver: FltCreateCommunicationPort failed with status 0x%x\n"), status);
        FltUnregisterFilter(gFilterHandle);
        return status;
    }

    // Запуск фильтра
    status = FltStartFiltering(gFilterHandle);
    if (!NT_SUCCESS(status)) {
        PT_DBG_PRINTF(PTDBG_TRACE_ROUTINES, ("FileControllerDriver: FltStartFiltering failed with status 0x%x\n"), status);
        FltCloseCommunicationPort(gServerPort);
        FltUnregisterFilter(gFilterHandle);
        return status;
    }

    return STATUS_SUCCESS;
}

// Выгрузка драйвера
NTSTATUS FileControllerUnload(
    _In_ FLT_FILTER_UNLOAD_FLAGS Flags
)
{
    UNREFERENCED_PARAMETER(Flags);

    PT_DBG_PRINT(PTDBG_TRACE_ROUTINES, ("FileControllerDriver: FileControllerUnload\n"));

    // Закрытие порта коммуникации
    if (gServerPort != NULL) {
        FltCloseCommunicationPort(gServerPort);
    }

    // Освобождение списков файлов и программ
    PLIST_ENTRY entry;
    PPROTECTED_FILE protectedFile;
    PTRUSTED_PROGRAM trustedProgram;

    ExAcquireFastMutex(&g_ListLock);

    while (!IsListEmpty(&g_ProtectedFilesList)) {
        entry = RemoveHeadList(&g_ProtectedFilesList);
        protectedFile = CONTAINING_RECORD(entry, PROTECTED_FILE, ListEntry);
        RtlFreeUnicodeString(&protectedFile->FileName);
        ExFreePool(protectedFile);
    }

    while (!IsListEmpty(&g_TrustedProgramsList)) {
        entry = RemoveHeadList(&g_TrustedProgramsList);
        trustedProgram = CONTAINING_RECORD(entry, TRUSTED_PROGRAM, ListEntry);
        RtlFreeUnicodeString(&trustedProgram->ProgramName);
        ExFreePool(trustedProgram);
    }

    ExReleaseFastMutex(&g_ListLock);

    // Отмена регистрации фильтра
    FltUnregisterFilter(gFilterHandle);

    return STATUS_SUCCESS;
}

// Настройка экземпляра
NTSTATUS FileControllerInstanceSetup(
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _In_ FLT_INSTANCE_SETUP_FLAGS Flags,
    _In_ DEVICE_TYPE VolumeDeviceType,
    _In_ FLT_FILESYSTEM_TYPE VolumeFilesystemType
)
{
    UNREFERENCED_PARAMETER(FltObjects);
    UNREFERENCED_PARAMETER(Flags);
    UNREFERENCED_PARAMETER(VolumeDeviceType);
    UNREFERENCED_PARAMETER(VolumeFilesystemType);

    PT_DBG_PRINT(PTDBG_TRACE_ROUTINES, ("FileControllerDriver: FileControllerInstanceSetup\n"));

    return STATUS_SUCCESS;
}

// Отключение экземпляра
VOID FileControllerInstanceTeardownStart(
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _In_ FLT_INSTANCE_TEARDOWN_FLAGS Flags
)
{
    UNREFERENCED_PARAMETER(FltObjects);
    UNREFERENCED_PARAMETER(Flags);

    //PT_DBG_PRINT(PTDBG_TRACE_ROUTINES, ("FileControllerDriver: FileControllerInstanceTeardownStart\n"));
}

// Завершение отключения экземпляра
VOID FileControllerInstanceTeardownComplete(
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _In_ FLT_INSTANCE_TEARDOWN_FLAGS Flags
)
{
    UNREFERENCED_PARAMETER(FltObjects);
    UNREFERENCED_PARAMETER(Flags);

    //PT_DBG_PRINT(PTDBG_TRACE_ROUTINES, ("FileControllerDriver: FileControllerInstanceTeardownComplete\n"));
}

// Запрос на отключение экземпляра
NTSTATUS FileControllerInstanceQueryTeardown(
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _In_ FLT_INSTANCE_QUERY_TEARDOWN_FLAGS Flags
)
{
    UNREFERENCED_PARAMETER(FltObjects);
    UNREFERENCED_PARAMETER(Flags);

    //PT_DBG_PRINT(PTDBG_TRACE_ROUTINES, ("FileControllerDriver: FileControllerInstanceQueryTeardown\n"));

    return STATUS_SUCCESS;
}

// Обработка операций до выполнения
FLT_PREOP_CALLBACK_STATUS FileControllerPreOperation(
    _Inout_ PFLT_CALLBACK_DATA Data,
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _Flt_CompletionContext_Outptr_ PVOID* CompletionContext
)
{
    NTSTATUS status;
    PFLT_FILE_NAME_INFORMATION nameInfo = NULL;
    PUNICODE_STRING processName = { 0 };
    PEPROCESS process;
    BOOLEAN isFileProtected = FALSE;
    BOOLEAN isTrusted = FALSE;
    ULONG requestType = 0;
    ACCESS_REPLY reply;


    UNREFERENCED_PARAMETER(CompletionContext);

    // Проверка только для операций чтения, записи и удаления
    switch (Data->Iopb->MajorFunction) {
    case IRP_MJ_READ:
        requestType = FILESYSTEM_READ;
        break;
    case IRP_MJ_WRITE:
        requestType = FILESYSTEM_WRITE;
        break;
    case IRP_MJ_SET_INFORMATION:
        if (Data->Iopb->Parameters.SetFileInformation.FileInformationClass == FileDispositionInformation ||
            Data->Iopb->Parameters.SetFileInformation.FileInformationClass == FileDispositionInformationEx) {
            requestType = FILESYSTEM_DELETE;
        }
        break;
    default:
        return FLT_PREOP_SUCCESS_NO_CALLBACK;
    }

    if (requestType == 0) {
        return FLT_PREOP_SUCCESS_NO_CALLBACK;
    }

    // Получаем имя файла
    status = FltGetFileNameInformation(
        Data,
        FLT_FILE_NAME_NORMALIZED | FLT_FILE_NAME_QUERY_DEFAULT,
        &nameInfo
    );

    if (!NT_SUCCESS(status)) {
        //PT_DBG_PRINTF(PTDBG_TRACE_ROUTINES, "FileControllerDriver: failed to get filenameinfo: %d\n", status);
        return FLT_PREOP_SUCCESS_NO_CALLBACK;
    }

    status = FltParseFileNameInformation(nameInfo);
    if (!NT_SUCCESS(status)) {
        PT_DBG_PRINTF(PTDBG_TRACE_ROUTINES, "FileControllerDriver: failed to parse filenameinfo: %d\n", status);
        FltReleaseFileNameInformation(nameInfo);
        return FLT_PREOP_SUCCESS_NO_CALLBACK;
    }


    isFileProtected = IsFileProtected(&nameInfo->Name);

    if (isFileProtected) {
        PT_DBG_PRINTF(PTDBG_TRACE_ROUTINES, ("FileControllerDriver: got file request to %wZ\n"), nameInfo->Name);
        process = IoThreadToProcess(Data->Thread);
        
        if (NT_SUCCESS(GetProcessFullImagePath(process, &processName)) && processName != NULL) {

            PT_DBG_PRINTF(PTDBG_TRACE_ROUTINES, ("FileControllerDriver: request from %wZ\n"), *processName);
            isTrusted = IsProgramTrusted(processName);

            if (!isTrusted) {
                status = AskUserForPermission(
                    requestType,
                    &nameInfo->Name,
                    processName,
                    &reply
                );

                if (NT_SUCCESS(status)) {
                    PT_DBG_PRINTF(PTDBG_TRACE_ROUTINES, ("FileControllerDriver: user answered: %d, "),
                        reply.ReplyType);
                    switch (reply.ReplyType) {
                    case RESPONSE_TYPE_ACCESS_GRANTED: // Разрешить
                        DPRINT("granted access\n");
                        FltReleaseFileNameInformation(nameInfo);
                        ExFreePool(processName);
                        ObDereferenceObject(process);
                        return FLT_PREOP_SUCCESS_NO_CALLBACK;

                    case RESPONSE_TYPE_ACCESS_DENIED: // Отклонить
                        DPRINT("denied access\n");
                        FltReleaseFileNameInformation(nameInfo);
                        ExFreePool(processName);
                        ObDereferenceObject(process);
                        Data->IoStatus.Status = STATUS_ACCESS_DENIED;
                        return FLT_PREOP_COMPLETE;

                    case RESPONSE_TYPE_ADD_TRUSTED: // Добавить в доверенные
                        DPRINT("TRUSTED\n");
                        ; PTRUSTED_PROGRAM trustedProgram = ExAllocatePool2(POOL_FLAG_PAGED, sizeof(TRUSTED_PROGRAM), 'TSRT'); // tag-(reversed)TSRT-TRST-TrustedProg
                        if (trustedProgram != NULL) {
                            trustedProgram->ProgramName.Buffer = ExAllocatePool2(POOL_FLAG_PAGED, processName->Length, 'NPRT'); // tag-(reversed)NPRT-TRPN-TrustedProgName
                            if (trustedProgram->ProgramName.Buffer != NULL) {
                                trustedProgram->ProgramName.Length = processName->Length;
                                trustedProgram->ProgramName.MaximumLength = processName->Length;
                                RtlCopyMemory(trustedProgram->ProgramName.Buffer, processName->Buffer, processName->Length);

                                ExAcquireFastMutex(&g_ListLock);
                                InsertTailList(&g_TrustedProgramsList, &trustedProgram->ListEntry);
                                ExReleaseFastMutex(&g_ListLock);
                            }
                            else {
                                ExFreePool(trustedProgram);
                            }
                        }
                        FltReleaseFileNameInformation(nameInfo);
                        ExFreePool(processName);
                        ObDereferenceObject(process);
                        return FLT_PREOP_SUCCESS_NO_CALLBACK;

                    case RESPONSE_TYPE_BLACKLIST: // Запретить программу
                        DPRINT("BLACKLISTED\n");
                        FltReleaseFileNameInformation(nameInfo);
                        ExFreePool(processName);
                        ObDereferenceObject(process);
                        Data->IoStatus.Status = STATUS_ACCESS_DENIED;
                        return FLT_PREOP_COMPLETE;
                    default:
                        DPRINT("FileControllerDriver: unknown message type\n");
                        break;
                    }
                }
            }
            ExFreePool2(processName, 'proc', NULL, 0);
        }
        ObDereferenceObject(process);
    }

    FltReleaseFileNameInformation(nameInfo);
    return FLT_PREOP_SUCCESS_NO_CALLBACK;
}

// Обработка операций после выполнения
FLT_POSTOP_CALLBACK_STATUS FileControllerPostOperation(
    _Inout_ PFLT_CALLBACK_DATA Data,
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _In_opt_ PVOID CompletionContext,
    _In_ FLT_POST_OPERATION_FLAGS Flags
)
{
    UNREFERENCED_PARAMETER(Data);
    UNREFERENCED_PARAMETER(FltObjects);
    UNREFERENCED_PARAMETER(CompletionContext);
    UNREFERENCED_PARAMETER(Flags);

    return FLT_POSTOP_FINISHED_PROCESSING;
}

// Проверка, является ли файл защищенным
BOOLEAN IsFileProtected(
    _In_ PUNICODE_STRING FileName
)
{
    PLIST_ENTRY entry;
    PPROTECTED_FILE protectedFile;
    BOOLEAN isProtected = FALSE;
    /*const WCHAR _dbg_exclude[] = L"Windows";
    const USHORT _dbg_exclude_len = 7;
    const WCHAR _dbg_exclude2[] = L"Program";
    const USHORT _dbg_exclude2_len = 7;
    const WCHAR _dbg_exclude3[] = L"Temp";
    const USHORT _dbg_exclude3_len = 4;
    if (ContainsSubstringWcharArray(FileName, _dbg_exclude, _dbg_exclude_len)) {
        return FALSE;
    }
    if (ContainsSubstringWcharArray(FileName, _dbg_exclude2, _dbg_exclude2_len)) {
        return FALSE;
    }
    if (ContainsSubstringWcharArray(FileName, _dbg_exclude3, _dbg_exclude3_len)) {
        return FALSE;
    }*/
    //PT_DBG_PRINTF(PTDBG_TRACE_ROUTINES, "checking is %wZ protected\n", FileName);

    ExAcquireFastMutex(&g_ListLock);

    for (entry = g_ProtectedFilesList.Flink; entry != &g_ProtectedFilesList; entry = entry->Flink) {
        protectedFile = CONTAINING_RECORD(entry, PROTECTED_FILE, ListEntry);
        if (RtlEqualUnicodeString(FileName, &protectedFile->FileName, TRUE)) {
            isProtected = TRUE;
            break;
        }
    }

    ExReleaseFastMutex(&g_ListLock);

    return isProtected;
}

// Проверка, является ли программа доверенной
BOOLEAN IsProgramTrusted(
    _In_ PUNICODE_STRING ProgramName
)
{
    PLIST_ENTRY entry;
    PTRUSTED_PROGRAM trustedProgram;
    BOOLEAN isTrusted = FALSE;

    ExAcquireFastMutex(&g_ListLock);

    for (entry = g_TrustedProgramsList.Flink; entry != &g_TrustedProgramsList; entry = entry->Flink) {
        trustedProgram = CONTAINING_RECORD(entry, TRUSTED_PROGRAM, ListEntry);
        if (RtlEqualUnicodeString(ProgramName, &trustedProgram->ProgramName, TRUE)) {
            isTrusted = TRUE;
            break;
        }
    }

    ExReleaseFastMutex(&g_ListLock);

    return isTrusted;
}

// Запрос у пользователя разрешения
NTSTATUS AskUserForPermission(
    _In_ ULONG RequestType,
    _In_ PUNICODE_STRING FileName,
    _In_ PUNICODE_STRING ProgramName,
    _Out_ PACCESS_REPLY Reply
)
{
    NTSTATUS status = STATUS_SUCCESS;
    ACCESS_REQUEST request_ff;
    ULONG replyLength;
    PLARGE_INTEGER ptimeout;
#ifdef ACCESS_REQUEST_MESSAGE_REPLY_TIMEOUT
    LARGE_INTEGER timeout;
    timeout.QuadPart = ACCESS_REQUEST_MESSAGE_REPLY_TIMEOUT;
    ptimeout = &timeout;
#else
    ptimeout = NULL;
#endif
    PT_DBG_PRINTF(PTDBG_TRACE_ROUTINES,
        "FileControllerDriver: asking for permisison to %d for %wZ (rq from %wZ)\n", RequestType, FileName, ProgramName);
    // Если клиент не подключен, запрещаем доступ
    if (gClientPort == NULL) {
        DPRINT("FileControllerDriver: userApp not connected, rejecting request\n");
        Reply->ReplyType = DEFAULT_RESPONSE_TYPE; // Отклонить
        return STATUS_SUCCESS;
    }

    // Заполняем запрос
    request_ff.RequestType = RequestType;
    RtlZeroMemory(request_ff.FileName, sizeof(request_ff.FileName));
    RtlZeroMemory(request_ff.ProgramName, sizeof(request_ff.ProgramName));

    if (FileName->Length / sizeof(WCHAR) < 256) {
        RtlCopyMemory(request_ff.FileName, FileName->Buffer, FileName->Length);
    }
    else {
        RtlCopyMemory(request_ff.FileName, FileName->Buffer, 255 * sizeof(WCHAR));
    }

    if (ProgramName->Length / sizeof(WCHAR) < 256) {
        RtlCopyMemory(request_ff.ProgramName, ProgramName->Buffer, ProgramName->Length);
    }
    else {
        RtlCopyMemory(request_ff.ProgramName, ProgramName->Buffer, 255 * sizeof(WCHAR));
    }
    DPRINT("FileControllerDriver: Got request to handle, sending to port\n");
    // Отправляем запрос пользовательскому режиму
    replyLength = sizeof(ACCESS_REPLY);

    status = FltSendMessage(
        gFilterHandle,
        &gClientPort,
        &request_ff,
        FLT_MSG_ACCESS_REQUEST_SIZE,
        Reply,
        &replyLength,
        ptimeout
    );
    PT_DBG_PRINTF(PTDBG_TRACE_ROUTINES, ("FileControllerDriver: Got status response: %d\n"), Reply->ReplyType);
    //LOG_BYTES(Reply, replyLength);
    if (!NT_SUCCESS(status) || status == STATUS_TIMEOUT) {
        if (status == STATUS_TIMEOUT)
            PT_DBG_PRINT(PTDBG_TRACE_ROUTINES, ("FileControllerDriver: status response timeout, using default REJECT action\n"));
        else
            PT_DBG_PRINTF(PTDBG_TRACE_ROUTINES, ("FileControllerDriver: status reponse non-success: %d\n"), status);
        Reply->ReplyType = DEFAULT_RESPONSE_TYPE; // Отклонить по умолчанию при ошибке
    }

    return status;
}

// Обработка подключения клиента
NTSTATUS FileControllerConnect(
    _In_ PFLT_PORT ClientPort,
    _In_ PVOID ServerPortCookie,
    _In_reads_bytes_(SizeOfContext) PVOID ConnectionContext,
    _In_ ULONG SizeOfContext,
    _Outptr_result_maybenull_ PVOID* ConnectionCookie
)
{
    UNREFERENCED_PARAMETER(ServerPortCookie);
    UNREFERENCED_PARAMETER(ConnectionContext);
    UNREFERENCED_PARAMETER(SizeOfContext);
    UNREFERENCED_PARAMETER(ConnectionCookie);

    PT_DBG_PRINT(PTDBG_TRACE_ROUTINES, ("FileControllerDriver: FileControllerConnect\n"));

    gClientPort = ClientPort;
    return STATUS_SUCCESS;
}

// Обработка отключения клиента
VOID FileControllerDisconnect(
    _In_opt_ PVOID ConnectionCookie
)
{
    UNREFERENCED_PARAMETER(ConnectionCookie);

    PT_DBG_PRINT(PTDBG_TRACE_ROUTINES, ("FileControllerDriver: FileControllerDisconnect\n"));

    FltCloseClientPort(gFilterHandle, &gClientPort);
    gClientPort = NULL;
}

// Обработка сообщений от клиента
NTSTATUS FileControllerMessage(
    _In_ PVOID ConnectionCookie,
    _In_reads_bytes_opt_(InputBufferSize) PVOID InputBuffer,
    _In_ ULONG InputBufferSize,
    _Out_writes_bytes_to_opt_(OutputBufferSize, *ReturnOutputBufferSize) PVOID OutputBuffer,
    _In_ ULONG OutputBufferSize,
    _Out_ PULONG ReturnOutputBufferSize
)
{
    NTSTATUS status = STATUS_SUCCESS;
    PPROTECTED_FILE protectedFile;
    PTRUSTED_PROGRAM trustedProgram;
    PUNICODE_STRING fileName;
    PUNICODE_STRING programName;
    PLIST_ENTRY entry;
    BOOLEAN found;

    UNREFERENCED_PARAMETER(ConnectionCookie);
    UNREFERENCED_PARAMETER(OutputBufferSize);

    PT_DBG_PRINT(PTDBG_TRACE_ROUTINES, ("FileControllerDriver: FileControllerMessage\n"));

    if (InputBuffer == NULL || InputBufferSize < sizeof(FLT_INC_MSG_STRUCT_SIZE) ||
        OutputBuffer == NULL || OutputBufferSize < sizeof(FLT_INC_RPL_STRUCT_SIZE)) {
        PT_DBG_PRINT(PTDBG_TRACE_ROUTINES, ("FileControllerDriver: Returning E_INVALID_PARAMETER:\n"));

        if (InputBuffer == NULL)
            //PT_DBG_PRINTF(PTDBG_TRACE_ROUTINES, (""), 123);
            PT_DBG_PRINT(PTDBG_TRACE_ROUTINES, ("FileControllerDriver: Invalid input buffer (null)\n"));
        if (InputBufferSize < sizeof(FLT_INC_MSG_STRUCT_SIZE))
            PT_DBG_PRINTF(PTDBG_TRACE_ROUTINES, ("FileControllerDriver: Too small input buffer size: %u < %u\n"), 
                InputBufferSize, sizeof(FLT_INC_MSG_STRUCT_SIZE));
            //PT_DBG_PRINT(PTDBG_TRACE_ROUTINES, ("FileControllerDriver: Too small input buffer size\n"));
        if (OutputBuffer == NULL)
            PT_DBG_PRINT(PTDBG_TRACE_ROUTINES, ("FileControllerDriver: Invalid output buffer (null)\n"));
        if (OutputBufferSize < sizeof(FLT_INC_RPL_STRUCT_SIZE))
            PT_DBG_PRINTF(PTDBG_TRACE_ROUTINES, ("FileControllerDriver: Too small output buffer size: %u < %u\n"),
                OutputBufferSize, sizeof(FLT_INC_RPL_STRUCT_SIZE));
            //PT_DBG_PRINT(PTDBG_TRACE_ROUTINES, ("FileControllerDriver: Too small output buffer size\n"));


        return STATUS_INVALID_PARAMETER;
    }

    *ReturnOutputBufferSize = 0;

    // Декодирование входящего сообщения
    FILTER_INCOMING_MESSAGE_STRUCT incomingMsg = *(PFILTER_INCOMING_MESSAGE_STRUCT)InputBuffer;
    ULONG messageType = incomingMsg.messageType;
    ULONG responseType;
    //PFLT_INSTANCE fltI;
    //UNICODE_STRING volume;

    switch (messageType) {
    case MESSAGE_TYPE_ADD_FILE: // Добавить файл в защищенные
        fileName = (PUNICODE_STRING)((PUCHAR)InputBuffer + FLT_INC_MSG_STRUCT_SIZE);
        PT_DBG_PRINTF(PTDBG_TRACE_ROUTINES, ("FileControllerDriver: got request add file %wZ\n"), fileName);

        protectedFile = ExAllocatePool2(POOL_FLAG_PAGED, sizeof(PROTECTED_FILE), 'FTRP'); // tag-(reversed)FTRP-PRTF-ProtectedFile
        if (protectedFile == NULL) {
            DPRINT("FileControllerDriver: INSUFF_RES error while allocating PRTF\n");
            return STATUS_INSUFFICIENT_RESOURCES;
        }

        protectedFile->FileName.Buffer = ExAllocatePool2(POOL_FLAG_PAGED, fileName->Length, 'NFRP'); // tag-(reversed)NFRP-PRFN-ProtectedFileName
        if (protectedFile->FileName.Buffer == NULL) {
            ExFreePool(protectedFile);
            DPRINT("FileControllerDriver: INSUFF_RES error while allocating PRFN\n");
            return STATUS_INSUFFICIENT_RESOURCES;
        }

        protectedFile->FileName.Length = fileName->Length;
        protectedFile->FileName.MaximumLength = fileName->Length;
        RtlCopyMemory(protectedFile->FileName.Buffer, fileName->Buffer, fileName->Length);

        ExAcquireFastMutex(&g_ListLock);
        InsertTailList(&g_ProtectedFilesList, &protectedFile->ListEntry);
        ExReleaseFastMutex(&g_ListLock);

        /*volume.MaximumLength = 256;
        volume.Buffer = ExAllocatePool2(POOL_FLAG_PAGED, volume.MaximumLength, 'FBPV'); // tag-(reversed)FBPV-VPBF-VolumePathBuffer


        if (volume.Buffer != NULL) {
            if (NT_SUCCESS(ExtractPathVolume(&protectedFile->FileName, &volume))) {
                PT_DBG_PRINTF(PTDBG_TRACE_ROUTINES, "FileControllerDriver: extracted volume from path: %wZ\n", volume);
                if (NT_SUCCESS(GetInstanceForVolume(&volume, &fltI))) {
                    FlushCacheForFile(gFilterHandle, fltI, &protectedFile->FileName);
                }
                else {
                    DPRINT("FileControllerDriver: failed to get filter instance\n");
                }
            }
            else {
                DPRINT("FileControllerDriver: failed to extract path volume\n");
            }
            ExFreePool2(volume.Buffer, 'FBPV', NULL, 0);
        }
        else {
            DPRINT("FileControllerDriver: failed to allocate volume string buffer\n");
        }*/

        responseType = 1;
        (*(PFILTER_INCOMING_REPLY_STRUCT)OutputBuffer).responseType = responseType;
        *ReturnOutputBufferSize = FLT_INC_RPL_STRUCT_SIZE;
        break;

    case MESSAGE_TYPE_REMOVE_FILE: // Удалить файл из защищенных
        fileName = (PUNICODE_STRING)((PUCHAR)InputBuffer + FLT_INC_MSG_STRUCT_SIZE);
        PT_DBG_PRINTF(PTDBG_TRACE_ROUTINES, ("FileControllerDriver: got request rm file %wZ\n"), fileName);
        found = FALSE;

        ExAcquireFastMutex(&g_ListLock);

        for (entry = g_ProtectedFilesList.Flink; entry != &g_ProtectedFilesList; entry = entry->Flink) {
            protectedFile = CONTAINING_RECORD(entry, PROTECTED_FILE, ListEntry);
            if (RtlEqualUnicodeString(fileName, &protectedFile->FileName, TRUE)) {
                RemoveEntryList(entry);
                RtlFreeUnicodeString(&protectedFile->FileName);
                ExFreePool(protectedFile);
                found = TRUE;
                break;
            }
        }

        ExReleaseFastMutex(&g_ListLock);

        if (found)
            PT_DBG_PRINT(PTDBG_TRACE_ROUTINES, ("FileControllerDriver: found and removed file\n"));
        else
            PT_DBG_PRINT(PTDBG_TRACE_ROUTINES, ("FileControllerDriver: not found file to remove from list\n"));

        responseType = found ? 1 : 0;
        (*(PFILTER_INCOMING_REPLY_STRUCT)OutputBuffer).responseType = responseType;
        *ReturnOutputBufferSize = FLT_INC_RPL_STRUCT_SIZE;
        break;

    case MESSAGE_TYPE_ADD_PROGRAM: // Добавить программу в доверенные
        programName = (PUNICODE_STRING)((PUCHAR)InputBuffer + FLT_INC_MSG_STRUCT_SIZE);
        PT_DBG_PRINTF(PTDBG_TRACE_ROUTINES, ("FileControllerDriver: got request add prog %wZ\n"), programName);

        trustedProgram = ExAllocatePool2(POOL_FLAG_PAGED, sizeof(TRUSTED_PROGRAM), 'TSRT'); // tag-(reversed)TSRT-TRST-TrustedProg
        if (trustedProgram == NULL) {
            DPRINT("FileControllerDriver: INSUFF_RES error while allocating TRST\n");
            return STATUS_INSUFFICIENT_RESOURCES;
        }

        trustedProgram->ProgramName.Buffer = ExAllocatePool2(POOL_FLAG_PAGED, programName->Length, 'NPRT'); // tag-(reversed)NPRT-TRPN-TrustedProgName
        if (trustedProgram->ProgramName.Buffer == NULL) {
            DPRINT("FileControllerDriver: INSUFF_RES error while allocating TRPN\n");
            ExFreePool(trustedProgram);
            return STATUS_INSUFFICIENT_RESOURCES;
        }

        trustedProgram->ProgramName.Length = programName->Length;
        trustedProgram->ProgramName.MaximumLength = programName->Length;
        RtlCopyMemory(trustedProgram->ProgramName.Buffer, programName->Buffer, programName->Length);

        ExAcquireFastMutex(&g_ListLock);
        InsertTailList(&g_TrustedProgramsList, &trustedProgram->ListEntry);
        ExReleaseFastMutex(&g_ListLock);

        responseType = 1;
        (*(PFILTER_INCOMING_REPLY_STRUCT)OutputBuffer).responseType = responseType;
        *ReturnOutputBufferSize = FLT_INC_RPL_STRUCT_SIZE;
        break;

    case MESSAGE_TYPE_REMOVE_PROGRAM: // Удалить программу из доверенных
        programName = (PUNICODE_STRING)((PUCHAR)InputBuffer + FLT_INC_MSG_STRUCT_SIZE);
        PT_DBG_PRINTF(PTDBG_TRACE_ROUTINES, ("FileControllerDriver: got request rm prog %wZ\n"), programName);
        found = FALSE;

        ExAcquireFastMutex(&g_ListLock);

        for (entry = g_TrustedProgramsList.Flink; entry != &g_TrustedProgramsList; entry = entry->Flink) {
            trustedProgram = CONTAINING_RECORD(entry, TRUSTED_PROGRAM, ListEntry);
            if (RtlEqualUnicodeString(programName, &trustedProgram->ProgramName, TRUE)) {
                RemoveEntryList(entry);
                RtlFreeUnicodeString(&trustedProgram->ProgramName);
                ExFreePool(trustedProgram);
                found = TRUE;
                break;
            }
        }

        ExReleaseFastMutex(&g_ListLock);

        if (found)
            PT_DBG_PRINT(PTDBG_TRACE_ROUTINES, ("FileControllerDriver: found and removed prog\n"));
        else
            PT_DBG_PRINT(PTDBG_TRACE_ROUTINES, ("FileControllerDriver: not found prog to remove\n"));

        responseType = found ? 1 : 0;
        (*(PFILTER_INCOMING_REPLY_STRUCT)OutputBuffer).responseType = responseType;
        *ReturnOutputBufferSize = FLT_INC_RPL_STRUCT_SIZE;
        break;

    default:
        PT_DBG_PRINTF(PTDBG_TRACE_ROUTINES, ("FileControllerDriver: got unknown messageType param: %d\n"), messageType);
        status = STATUS_INVALID_PARAMETER;
        break;
    }

    return status;
}