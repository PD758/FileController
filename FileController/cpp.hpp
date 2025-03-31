/*
*
* github.com/PD758
* All rights reserved. 2025.
*
*/
#pragma once

//#include <windows.h>

#include "c_specific.h"

#include <string>
#include <vector>
#include <thread>
#include <queue>
#include <atomic>
#include <memory>

#include "c_common.h"

/* Structs definitions */

struct ProtectedFile {
    std::wstring path;
};

struct TrustedProgram {
    std::wstring path;
};

class CriticalSection {
    CRITICAL_SECTION sec;
public:
    CriticalSection();
    ~CriticalSection();

    void acquire();
    bool try_acquire();
    void release();
    PCRITICAL_SECTION _raw();
};
class CriticalLockGuard {
    CriticalSection& sec;
public:
    CriticalLockGuard(CriticalSection&);
    ~CriticalLockGuard();
};
class CriticalConditionVariable {
    CONDITION_VARIABLE cv;
    CriticalSection* pCs;
public:
    CriticalConditionVariable();
    CriticalConditionVariable(CriticalSection&);
    ~CriticalConditionVariable() {}
    void wait(DWORD timeout = INFINITY);
    void notify_one();
    void notify_all();
};

struct DriverConnProtector {
    bool conn_closed = true;
    DriverConnProtector() = default;
    ~DriverConnProtector();
    void arm();
    void disarm();
};

/* C-Functions declarations */

/*extern "C" {
    BOOL InitializeDriver();
    void CleanupDriver();
    BOOL SendMessageToDriver(ULONG messageType, const wchar_t* data);
}*/

/* Global variables */

extern std::vector<ProtectedFile> g_ProtectedFiles;
extern std::vector<TrustedProgram> g_TrustedPrograms;
//extern std::mutex g_Mutex;
extern CriticalSection g_SMutex;
extern CriticalSection g_RQMutex;
extern CriticalConditionVariable g_RQ_CV;
extern std::queue<FILTER_MESSAGE_ACCESS_REQUEST> g_RequestQueue;
extern std::atomic<bool> g_Running;
extern HWND g_hMainWnd;
extern HWND g_hProtectedList;
extern HWND g_hTrustedList;
extern UINT g_TimeoutDuration;
extern ULONG g_DefaultAction;
extern HINSTANCE g_hInstance;

/* Function declarations */

// Global variables manipulation
BOOL AddProtectedFile(const std::wstring& filePath);
BOOL RemoveProtectedFile(const std::wstring& filePath);
BOOL AddTrustedProgram(const std::wstring& programPath);
BOOL RemoveTrustedProgram(const std::wstring& programPath);

// Dialog callbacks
INT_PTR CALLBACK MainDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
INT_PTR CALLBACK RequestDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
INT_PTR CALLBACK SettingsDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
DWORD WINAPI MessageThread(LPVOID lpParam);
DWORD WINAPI RequestQHandler(LPVOID lpParam);

// System UI interactions
// Show system file picker request to any file
BOOL BrowseForFile(HWND hWnd, std::wstring& filePath);
// Show system file picker request to .exe/.bat/.com file
BOOL BrowseForProgram(HWND hWnd, std::wstring& programPath);

// Convert "C:\Folder\File.ext" path to "\Device\HarddiskVolumeX\Folder\File.ext"
std::wstring ConvertToNtPath(const std::wstring& filePath) noexcept;
// Convert "\Device\HarddiskVolumeX\Folder\File.ext" path to "C:\Folder\File.ext"
std::wstring ConvertToDosPath(const std::wstring& filePath) noexcept;

// .INI config file
// load from %appdata%\\FileController\\settings.ini
BOOL LoadSettings(PSettings settings);
// save to %appdata%\\FileController\\settings.ini
BOOL SaveSettings(PSettings settings);

