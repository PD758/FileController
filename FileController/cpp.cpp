/*
*
* github.com/PD758
* All rights reserved. 2025.
*
*/
#include <windows.h>
#include <fltuser.h>
#include <commctrl.h>
#include <shlobj.h>
#include <shlwapi.h>

#include "c_common.h"
#include "cpp.hpp"

#include "resource.h"

template<typename CharT>
static bool starts_with(const std::basic_string<CharT>& str, const std::basic_string<CharT>& prefix) {
    if (prefix.size() > str.size())
        return false;
    for (size_t i = 0; i < prefix.size(); i++)
        if (str[i] != prefix[i])
            return false;
    return true;
}

CriticalSection::CriticalSection() {
    InitializeCriticalSection(&this->sec);
}
CriticalSection::~CriticalSection() {
    DeleteCriticalSection(&this->sec);
}
void CriticalSection::acquire() {
    EnterCriticalSection(&this->sec);
}
void CriticalSection::release() {
    LeaveCriticalSection(&this->sec);
}
bool CriticalSection::try_acquire() {
    return TryEnterCriticalSection(&this->sec);
}
PCRITICAL_SECTION CriticalSection::_raw() {
    return &this->sec;
}

CriticalLockGuard::CriticalLockGuard(CriticalSection& crt) : sec(crt) {
    crt.acquire();
}
CriticalLockGuard::~CriticalLockGuard() {
    this->sec.release();
}
CriticalConditionVariable::CriticalConditionVariable() : pCs(new CriticalSection()) {
    InitializeConditionVariable(&this->cv);
}
CriticalConditionVariable::CriticalConditionVariable(CriticalSection& externalCs) : pCs(&externalCs) {
    InitializeConditionVariable(&this->cv);
}
void CriticalConditionVariable::wait(DWORD timeout) {
    SleepConditionVariableCS(&this->cv, this->pCs->_raw(), timeout);
}
void CriticalConditionVariable::notify_one() {
    WakeConditionVariable(&this->cv);
}
void CriticalConditionVariable::notify_all() {
    WakeAllConditionVariable(&this->cv);
}

DriverConnProtector::~DriverConnProtector() {
    if (!this->conn_closed) {
        CleanupDriver();
        LOG("Closed driver handle by protector");
    }
}
void DriverConnProtector::arm() {
    this->conn_closed = false;
}
void DriverConnProtector::disarm() {
    this->conn_closed = true;
}

std::wstring ConvertToNtPath(const std::wstring& filePath) noexcept {
    if (filePath.size() < 3 || filePath[1] != L':' || filePath[2] != L'\\')
        return filePath;

    std::wstring driveLetter = filePath.substr(0, 2); // "C:"
    wchar_t deviceName[MAX_PATH] = { 0 };

    if (QueryDosDevice(driveLetter.c_str(), deviceName, MAX_PATH) == 0)
        return filePath;

    return std::wstring(deviceName) + filePath.substr(2);
}
std::wstring ConvertToDosPath(const std::wstring& ntPath) noexcept {
    //LOG("ConvertToDosPath: start");
    wchar_t drives[MAX_PATH] = { 0 };
    if (GetLogicalDriveStrings(MAX_PATH, drives) == 0)
        return ntPath;

    for (wchar_t* drive = drives; *drive; drive += wcslen(drive) + 1) {
        wchar_t deviceName[MAX_PATH] = { 0 };
        wchar_t symbol[3] = { 0 };
        symbol[0] = *drive;
        symbol[1] = L':';
        //WLogf(L"ConvertToDosPath: trying to check %s\n", symbol);
        if (QueryDosDevice(symbol, deviceName, MAX_PATH) == 0) {
            FLogf("ConvertToDosPath: query error %d\n", GetLastError());
            continue;
        }
        std::wstring device(deviceName);
        //WLogf(L"ConvertToDosPath: checking %s\n\n", device.c_str());
        if (starts_with(ntPath, device))
            return std::wstring(drive) + ntPath.substr(device.size() + 1);
    }
    return ntPath;
}

BOOL BrowseForFile(HWND hWnd, std::wstring& filePath) {
    OPENFILENAMEW ofn = { 0 };
    WCHAR szFile[MAX_PATH] = { 0 };

    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hWnd;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile) / sizeof(szFile[0]);
    ofn.lpstrFilter = L"All Files (*.*)\0*.*\0";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;

    if (GetOpenFileNameW(&ofn) == TRUE) {
        filePath = szFile;
        return TRUE;
    }
    return FALSE;
}

BOOL BrowseForProgram(HWND hWnd, std::wstring& programPath) {
    OPENFILENAMEW ofn = { 0 };
    WCHAR szFile[MAX_PATH] = { 0 };

    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hWnd;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile) / sizeof(szFile[0]);
    ofn.lpstrFilter = L"Executable Files (*.exe;*.com;*.bat;*.vbs;*.cmd)\0*.exe;*.com;*.bat;*.vbs;*.cmd\0All Files (*.*)\0*.*\0";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;

    if (GetOpenFileNameW(&ofn) == TRUE) {
        programPath = szFile;
        return TRUE;
    }
    return FALSE;
}

BOOL LoadSettings(PSettings settings) {
    LOG("LoadSettings: Loading settings");
    PWSTR appDataPath = NULL;
    HRESULT hr = SHGetKnownFolderPath(FOLDERID_RoamingAppData, KF_FLAG_CREATE, NULL, &appDataPath);
    if (FAILED(hr)) {
        LOG("LoadSettings: failed to get appdata folder");
        return FALSE;
    }

    std::wstring settingsFilePath = appDataPath;
    settingsFilePath += L"\\FileController\\settings.ini";
    CoTaskMemFree(appDataPath); // Free allocated memory

    settings->timeoutDuration = GetPrivateProfileIntW(L"Settings", L"Timeout", 15, settingsFilePath.c_str()) * 1000; // in ms
    settings->defaultAction = GetPrivateProfileIntW(L"Settings", L"DefaultAction", 2, settingsFilePath.c_str()); // 1 or 2
    LOG("LoadSettings: success");
    return TRUE;
}

BOOL SaveSettings(PSettings settings) {
    LOG("SaveSettings: Saving settings");
    PWSTR appDataPath = NULL;
    HRESULT hr = SHGetKnownFolderPath(FOLDERID_RoamingAppData, KF_FLAG_CREATE, NULL, &appDataPath);
    if (FAILED(hr)) {
        LOG("SaveSettings: failed to get appdata folder");
        return FALSE;
    }

    std::wstring directoryPath = appDataPath;
    directoryPath += L"\\FileController";
    CoTaskMemFree(appDataPath); // Free allocated memory

    if (!CreateDirectoryW(directoryPath.c_str(), NULL) && GetLastError() != ERROR_ALREADY_EXISTS) {
        FLogf("SaveSettings: failed to create dir, error code %d\n", GetLastError());
        return FALSE;
    }

    std::wstring settingsFilePath = directoryPath + L"\\settings.ini";

    WritePrivateProfileStringW(L"Settings", L"Timeout", std::to_wstring(settings->timeoutDuration / 1000).c_str(), settingsFilePath.c_str());
    WritePrivateProfileStringW(L"Settings", L"DefaultAction", std::to_wstring(settings->defaultAction).c_str(), settingsFilePath.c_str());
    LOG("SaveSettings: success");
    return TRUE;
}

INT_PTR CALLBACK MainDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {
    //LOG("main dialog proc");
    switch (message) {
    case WM_INITDIALOG:
    {
        LOG("MainDlgProc: init");
        g_hMainWnd = hDlg;
        g_hProtectedList = GetDlgItem(hDlg, IDC_PROTECTED_LIST);
        g_hTrustedList = GetDlgItem(hDlg, IDC_TRUSTED_LIST);

        // Init list views
        LOG("MainDlgProc: init views");
        ListView_SetExtendedListViewStyle(g_hProtectedList, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
        ListView_SetExtendedListViewStyle(g_hTrustedList, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);

        // Init columns for protected files list
        LOG("MainDlgProc: init columns files");
        LVCOLUMN lvc = { 0 };
        lvc.mask = LVCF_TEXT | LVCF_WIDTH;
        lvc.cx = 450;
        wchar_t stringFilePath[] = L"File Path";
        lvc.pszText = stringFilePath;
        ListView_InsertColumn(g_hProtectedList, 0, &lvc);

        // Init columns for trusted programs list
        LOG("MainDlgProc: init columns programs");
        wchar_t stringProgramPath[] = L"Program Path";
        lvc.pszText = stringProgramPath;
        ListView_InsertColumn(g_hTrustedList, 0, &lvc);

        // Fill list views
        LOG("MainDlgProc: fill views");
        //std::lock_guard<std::mutex> lock(g_Mutex);
        CriticalLockGuard lock(g_SMutex);
        LOG("MainDlgProc: acquired lock");
        for (const auto& file : g_ProtectedFiles) {
            LVITEM lvi = { 0 };
            lvi.mask = LVIF_TEXT;
            lvi.iItem = ListView_GetItemCount(g_hProtectedList); // Push back
            lvi.iSubItem = 0;
            lvi.pszText = (LPWSTR)file.path.c_str();
            ListView_InsertItem(g_hProtectedList, &lvi);
        }

        for (const auto& program : g_TrustedPrograms) {
            LVITEM lvi = { 0 };
            lvi.mask = LVIF_TEXT;
            lvi.iItem = ListView_GetItemCount(g_hTrustedList); // Push back
            lvi.iSubItem = 0;
            lvi.pszText = (LPWSTR)program.path.c_str();
            ListView_InsertItem(g_hTrustedList, &lvi);
        }
        LOG("MainDlgProc: init end");
        return TRUE;
    }
    case WM_COMMAND:
    {
        switch (LOWORD(wParam)) {
        case IDC_ADD_FILE: // Add file button clicked
        {
            LOG("MainDlgProc: add file button clicked");
            std::wstring filePath;
            if (BrowseForFile(hDlg, filePath)) {
                if (AddProtectedFile(ConvertToNtPath(filePath))) {
                    LVITEM lvi = { 0 };
                    lvi.mask = LVIF_TEXT;
                    lvi.iItem = ListView_GetItemCount(g_hProtectedList); // Push back
                    lvi.iSubItem = 0;
                    lvi.pszText = (LPWSTR)filePath.c_str();
                    ListView_InsertItem(g_hProtectedList, &lvi);
                }
                else {
                    MessageBox(hDlg, L"Failed to add file to protected list", L"Error", MB_OK | MB_ICONERROR);
                }
            }
            return TRUE;
        }

        case IDC_REMOVE_FILE: // Remove file button clicked
        {
            LOG("MainDlgProc: remove file button clicked");
            int iItem = ListView_GetNextItem(g_hProtectedList, -1, LVNI_SELECTED);
            if (iItem != -1) {
                WCHAR filePath[MAX_PATH];
                LVITEM lvi = { 0 };
                lvi.iItem = iItem;
                lvi.iSubItem = 0;
                lvi.mask = LVIF_TEXT;
                lvi.pszText = filePath;
                lvi.cchTextMax = MAX_PATH;

                ListView_GetItem(g_hProtectedList, &lvi); // Get selected element

                if (RemoveProtectedFile(ConvertToNtPath(filePath))) {
                    ListView_DeleteItem(g_hProtectedList, iItem);
                }
                else {
                    MessageBox(hDlg, L"Failed to remove file from protected list", L"Error", MB_OK | MB_ICONERROR);
                }
            }
            else {
                MessageBox(hDlg, L"No file selected", L"Error", MB_OK | MB_ICONWARNING);
            }
            return TRUE;
        }

        case IDC_ADD_PROGRAM:
        {
            LOG("MainDlgProc: add program button clicked");
            std::wstring programPath;
            if (BrowseForProgram(hDlg, programPath)) {
                if (AddTrustedProgram(ConvertToNtPath(programPath))) {
                    LVITEM lvi = { 0 };
                    lvi.mask = LVIF_TEXT;
                    lvi.iItem = ListView_GetItemCount(g_hTrustedList); // Вставляем в конец списка
                    lvi.iSubItem = 0;
                    lvi.pszText = (LPWSTR)programPath.c_str();
                    ListView_InsertItem(g_hTrustedList, &lvi);
                }
                else {
                    MessageBox(hDlg, L"Failed to add program to trusted list", L"Error", MB_OK | MB_ICONERROR);
                }
            }
            return TRUE;
        }

        case IDC_REMOVE_PROGRAM:
        {
            LOG("MainDlgProc: remove program button clicked");
            int iItem = ListView_GetNextItem(g_hTrustedList, -1, LVNI_SELECTED);
            if (iItem != -1) {
                WCHAR programPath[MAX_PATH];
                LVITEM lvi = { 0 };
                lvi.iItem = iItem;
                lvi.iSubItem = 0;
                lvi.mask = LVIF_TEXT;
                lvi.pszText = programPath;
                lvi.cchTextMax = MAX_PATH;
                ListView_GetItem(g_hTrustedList, &lvi);

                if (RemoveTrustedProgram(ConvertToNtPath(programPath))) {
                    ListView_DeleteItem(g_hTrustedList, iItem);
                }
                else {
                    MessageBox(hDlg, L"Failed to remove program from trusted list", L"Error", MB_OK | MB_ICONERROR);
                }
            }
            else {
                MessageBox(hDlg, L"No program selected", L"Error", MB_OK | MB_ICONWARNING);
            }
            return TRUE;
        }
        case IDC_SETTINGS: // "Settings" button clicked
        {
            LOG("MainDlgProc: settings button clicked");
            DialogBox(GetModuleHandle(NULL), MAKEINTRESOURCE(IDD_SETTINGS), hDlg, SettingsDlgProc);
            return TRUE;
        }

        case IDOK: // pass
        case IDCANCEL: // exit button
            LOG("MainDlgProc: exit");
            EndDialog(hDlg, 0);
            return TRUE;
        }
        break;
    }
    case WM_CLOSE: // window closed
        LOG("MainDlgProc: window closed");
        EndDialog(hDlg, 0);
        return TRUE;

    default: // pass
        return FALSE;
    }
}

INT_PTR CALLBACK RequestDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {
    static PFILTER_MESSAGE_ACCESS_REQUEST pRequest = nullptr;
    static HWND hwndProgressBar = nullptr;
    static UINT timerId = 0;
    static int timeLeft = 0;

    // Window with close timeout with default action

    switch (message) {
    case WM_INITDIALOG:
    {
        LOG("RequestDlgProc: init");
        pRequest = (PFILTER_MESSAGE_ACCESS_REQUEST)lParam;

        // Init textboxes in dialog
        LOG("RequestDlgProc: init textboxes");
        std::wstring filename(pRequest->Data.FileName);
        std::wstring programname(pRequest->Data.ProgramName);

        filename = ConvertToDosPath(filename);
        programname = ConvertToDosPath(programname);

        SetDlgItemTextW(hDlg, IDC_FILE_NAME, filename.c_str());
        SetDlgItemTextW(hDlg, IDC_PROGRAM_NAME, programname.c_str());

        // Init choices labels
        LOG("RequestDlgProc: init labels");
        const wchar_t* operationType = L"Unknown";
        if (pRequest->Data.RequestType & FILESYSTEM_READ) operationType = L"Read";
        else if (pRequest->Data.RequestType & FILESYSTEM_WRITE) operationType = L"Write";
        else if (pRequest->Data.RequestType & FILESYSTEM_DELETE) operationType = L"Delete";
        SetDlgItemTextW(hDlg, IDC_OPERATION_TYPE, operationType);

        // Init progress bar
        LOG("RequestDlgProc: init progressbar");
        hwndProgressBar = GetDlgItem(hDlg, IDC_PROGRESS);
        SendMessage(hwndProgressBar, PBM_SETRANGE, 0, MAKELPARAM(0, g_TimeoutDuration / 1000)); // 0 - 15 сек
        SendMessage(hwndProgressBar, PBM_SETSTEP, 1, 0); // Шаг 1 (по секунде)

        timeLeft = g_TimeoutDuration;
        timerId = SetTimer(hDlg, 1, 1000, (TIMERPROC)nullptr); // 1 sec interval

        if (!timerId) {
            LOG("RequestDlgProc: timer set failed");
        }
        else {
            FLogf("RequestDlgProc: timer set, id=%d\n", timerId);
        }

        LOG("RequestDlgProc: init end");
        return TRUE;
    }
    case WM_TIMER: // timeout
    {
        //FLogf("RequestDlgProc: TIMER, %d left\n", timeLeft);
        timeLeft -= 1000;
        SendMessage(hwndProgressBar, PBM_STEPIT, 0, 0);
        if (timeLeft <= 0) {
            LOG("RequestDlgProc: timer ended, leaving with default action");
            KillTimer(hDlg, timerId);
            EndDialog(hDlg, g_DefaultAction);  // Return default action
            return TRUE;
        }
        break;
    }
    case WM_COMMAND: {
        switch (LOWORD(wParam)) {
        case ID_ALLOW:
            LOG("RequestDlgProc: allow action");
            KillTimer(hDlg, timerId);
            EndDialog(hDlg, RESPONSE_TYPE_ACCESS_GRANTED);  // Allow
            return TRUE;
        case ID_DENY:
            LOG("RequestDlgProc: deny action");
            KillTimer(hDlg, timerId);
            EndDialog(hDlg, RESPONSE_TYPE_ACCESS_DENIED);  // Deny
            return TRUE;
        case ID_ADD_TRUSTED:
            LOG("RequestDlgProc: TRUST action");
            KillTimer(hDlg, timerId);
            EndDialog(hDlg, RESPONSE_TYPE_ADD_TRUSTED);  // Add to trusted
            return TRUE;
        case ID_BLOCK_PROGRAM:
            LOG("RequestDlgProc: BLOCK action");
            KillTimer(hDlg, timerId);
            EndDialog(hDlg, RESPONSE_TYPE_BLACKLIST);  // Block Program
            return TRUE;
        case IDCANCEL:
            KillTimer(hDlg, timerId);
            LOG("RequestDlgProc: DEFAULT action");
            EndDialog(hDlg, g_DefaultAction);  // Return default action on cancel
            return TRUE;
        }
        break;
    }
    case WM_CLOSE:
        LOG("RequestDlgProc: DEFAULT action, windows closed");
        KillTimer(hDlg, timerId);
        EndDialog(hDlg, g_DefaultAction);  // Return default action on close
        return TRUE;
    case WM_DESTROY:
        LOG("RequestDlgProc: window destroyed");
        KillTimer(hDlg, timerId);
        EndDialog(hDlg, g_DefaultAction);
        return TRUE;
    default:
        return FALSE;
    }
}

INT_PTR CALLBACK SettingsDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_INITDIALOG:
    {
        LOG("SettingsDlgProc: init");
        // Init controls with current settings
        SetDlgItemInt(hDlg, IDC_TIMEOUT, g_TimeoutDuration / 1000, FALSE);  // Convert to seconds
        CheckRadioButton(hDlg, IDC_ALLOW_DEFAULT, IDC_DENY_DEFAULT,
            (g_DefaultAction == 1) ? IDC_ALLOW_DEFAULT : IDC_DENY_DEFAULT);
        LOG("SettingsDlgProc: init end");
        return TRUE;
    }
    case WM_COMMAND: {
        switch (LOWORD(wParam)) {
        case IDOK:
        {
            LOG("SettingsDlgProc: OK clicked");
            BOOL success;
            UINT timeout = GetDlgItemInt(hDlg, IDC_TIMEOUT, &success, FALSE);
            if (!success) {
                MessageBox(hDlg, L"Invalid timeout value", L"Error", MB_OK | MB_ICONERROR);
                return TRUE;
            }
            g_TimeoutDuration = timeout * 1000;  // Convert to milliseconds

            if (IsDlgButtonChecked(hDlg, IDC_ALLOW_DEFAULT) == BST_CHECKED) {
                g_DefaultAction = 1;
            }
            else {
                g_DefaultAction = 2;
            }

            // Save settings
            Settings settings;
            settings.timeoutDuration = g_TimeoutDuration;
            settings.defaultAction = g_DefaultAction;
            SaveSettings(&settings);

            EndDialog(hDlg, IDOK);
            return TRUE;
        }
        case IDCANCEL:
            LOG("SettingsDlgProc: canceled");
            EndDialog(hDlg, IDCANCEL);
            return TRUE;
        }
        break;
    }
    default:
        return FALSE;
    }
}

BOOL AddProtectedFile(const std::wstring& filePath) {
    FLogf("AddProtectedFile: adding %s\n", filePath.c_str());
    if (SendMessageToDriver(MESSAGE_TYPE_ADD_FILE, filePath.c_str())) {
        //std::lock_guard<std::mutex> lock(g_Mutex);
        CriticalLockGuard lock(g_SMutex);
        ProtectedFile file;
        file.path = filePath;
        g_ProtectedFiles.push_back(file);
        LOG("AddProtectedFile: success");
        return TRUE;
    }
    LOG("AddProtectedFile: failed");
    return FALSE;
}

BOOL RemoveProtectedFile(const std::wstring& filePath) {
    FLogf("RemoveProtectedFile: removing %s\n", filePath.c_str());
    if (SendMessageToDriver(MESSAGE_TYPE_REMOVE_FILE, filePath.c_str())) {
        //std::lock_guard<std::mutex> lock(g_Mutex);
        CriticalLockGuard lock(g_SMutex);
        g_ProtectedFiles.erase(std::remove_if(g_ProtectedFiles.begin(), g_ProtectedFiles.end(),
            [&](const ProtectedFile& file) { return _wcsicmp(file.path.c_str(), filePath.c_str()) == 0; }),
            g_ProtectedFiles.end());
        LOG("RemoveProtectedFile: success");
        return TRUE;
    }
    LOG("RemoveProtectedFile: failed");
    return FALSE;
}

BOOL AddTrustedProgram(const std::wstring& programPath) {
    FLogf("AddTrustedProgram: adding %s\n", programPath.c_str());
    if (SendMessageToDriver(MESSAGE_TYPE_ADD_PROGRAM, programPath.c_str())) {
        //std::lock_guard<std::mutex> lock(g_Mutex);
        CriticalLockGuard lock(g_SMutex);
        TrustedProgram program;
        program.path = programPath;
        g_TrustedPrograms.push_back(program);
        LOG("AddTrustedProgram: success");
        return TRUE;
    }
    LOG("AddTrustedProgram: failed");
    return FALSE;
}

BOOL RemoveTrustedProgram(const std::wstring& programPath) {
    FLogf("RemoveTrustedProgram: removing %s\n", programPath.c_str());
    if (SendMessageToDriver(MESSAGE_TYPE_REMOVE_PROGRAM, programPath.c_str())) {
        //std::lock_guard<std::mutex> lock(g_Mutex);
        CriticalLockGuard lock(g_SMutex);
        g_TrustedPrograms.erase(std::remove_if(g_TrustedPrograms.begin(), g_TrustedPrograms.end(),
            [&](const TrustedProgram& program) { return _wcsicmp(program.path.c_str(), programPath.c_str()) == 0; }),
            g_TrustedPrograms.end());
        LOG("RemoveTrustedProgram: success");
        return TRUE;
    }
    LOG("RemoveTrustedProgram: failed");
    return FALSE;
}

void ProcessAccessRequest(const FILTER_MESSAGE_ACCESS_REQUEST& request) noexcept {
    LOG("ProcessAccessRequest: got request to handle");

    if (!(request.Data.RequestType & (FILESYSTEM_READ | FILESYSTEM_WRITE | FILESYSTEM_DELETE))) {
        FLogf("ProcessAccessRequest: unknown request type %d, skipping\n");
        return;
    }

    TrustedProgram program;
    program.path = request.Data.ProgramName;
    program.path = ConvertToDosPath(program.path);

    int replyType;

    if (IsProgramBlocked(program.path)) {
        replyType = RESPONSE_TYPE_ACCESS_DENIED;
        LOG("ProcessAccessRequest: denied access by blacklist");
    }
    else {
        replyType = (int)DialogBoxParamW(g_hInstance, MAKEINTRESOURCEW(IDD_REQUEST),
            g_hMainWnd, RequestDlgProc, (LPARAM)&request);
        FLogf("ProcessAccessRequest: Dialog result: %d\n", replyType);
    }

    FILTER_REPLY_ACCESS_RESPONSE reply = { 0 };
    reply.Header.Status = 0;
    reply.Header.MessageId = request.Header.MessageId;
    reply.Data.ReplyType = static_cast<ULONG>(replyType);

    LOG("ProcessAccessRequest: replying with response...");
    HRESULT hr = FilterReplyMessage(GetPort(), (PFILTER_REPLY_HEADER)&reply, FLT_RPL_ACCESS_RESPONSE_SIZE);

    if (SUCCEEDED(hr)) {
        LOG("ProcessAccessRequest: request reply success");
        if (replyType == RESPONSE_TYPE_ADD_TRUSTED) {
            LOG("ProcessAccessRequest: adding trusted program");
            CriticalLockGuard lock(g_SMutex);
            g_TrustedPrograms.push_back(program);
            LVITEM lvi = { 0 };
            lvi.mask = LVIF_TEXT;
            lvi.iItem = ListView_GetItemCount(g_hTrustedList);
            lvi.iSubItem = 0;
            lvi.pszText = (LPWSTR)(program.path.c_str());
            ListView_InsertItem(g_hTrustedList, &lvi);
        }
        else if (replyType == RESPONSE_TYPE_BLACKLIST) {
            CriticalLockGuard lock(g_BlockedMutex);
            g_BlockedPrograms.insert(program.path);
        }
    }
    else {
        LOG("ProcessAccessRequest: request reply failed");
    }
}

DWORD WINAPI RequestQHandler(LPVOID lpParam) {
    LOG("RequestQHandler: thread start");
    while (true) {
        LOG("RequestQHandler: loop it");
        FILTER_MESSAGE_ACCESS_REQUEST task;
        {
            LOG("RequestQHandler: waiting for task");
            g_RQMutex.acquire();

            while (g_Running.load() && g_RequestQueue.empty()) {
                g_RQ_CV.wait();
            }

            LOG("RequestQHandler: got task");
            if (!g_Running.load() && g_RequestQueue.empty()) {
                LOG("RequestQHandler: leaving");
                g_RQMutex.release();
                break;
            }

            LOG("RequestQHandler: extracting task from queue");

            task = g_RequestQueue.front();
            g_RequestQueue.pop();
            g_RQMutex.release();
        }
        LOG("RequestQHandler: Handling task");
        ProcessAccessRequest(task);
    }
    return 0;
}

DWORD WINAPI MessageThread(LPVOID lpParam) {
    const static DWORD buffer_size = FLT_MSG_ACCESS_REQUEST_SIZE + 100;
    LOG("msgThread: thread start");
    while (g_Running.load()) {
        LOG("msgThread: loop it");
        FILTER_MESSAGE_ACCESS_REQUEST request = { 0 };
        UCHAR buffer[buffer_size];

        // Блокирующий вызов получения сообщения от фильтра.
        HRESULT hr = FilterGetMessage(GetPort(), (PFILTER_MESSAGE_HEADER)buffer,
            buffer_size, NULL);

        FLogf("msgThread: got message, hr=%d\n", hr);
        if (FAILED(hr)) {
            LOG("msgThread: hr check failed");
            continue;
        }
        else {
            LOG("msgThread: FAILED() check success");
            request = *(PFILTER_MESSAGE_ACCESS_REQUEST)buffer;
            WLogf(L"msgThread: got request: TYPE=%d, FILENAME=\"%s\", PROGNAME=\"%s\"\n",
                request.Data.RequestType, request.Data.FileName, request.Data.ProgramName);
        }
        g_RQMutex.acquire();
        g_RequestQueue.push(request);
        g_RQMutex.release();
        LOG("msgThread: pushed request to queue");
        g_RQ_CV.notify_one();
        LOG("msgThread: notified worker to handle request");
    }
    return 0;
}


BOOL IsProgramBlocked(const std::wstring& programPath) {
    CriticalLockGuard lock(g_BlockedMutex);
    return g_BlockedPrograms.find(programPath) != g_BlockedPrograms.end();
}

