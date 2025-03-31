/*
*
* github.com/PD758
* All rights reserved. 2025.
*
*/
#include <windows.h>
#include <fltuser.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <commctrl.h>

#include "shared.h"
#include "c_common.h"
#include "c_specific.h"


#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <memory>
#include <queue>

#include "cpp.hpp"


#include "resource.h"

#pragma comment(lib, "fltlib.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shlwapi.lib")

/* Global variables */

std::vector<ProtectedFile> g_ProtectedFiles;
std::vector<TrustedProgram> g_TrustedPrograms;
//std::mutex g_Mutex;
CriticalSection g_SMutex;
CriticalSection g_RQMutex; // request queue mutex;
CriticalConditionVariable g_RQ_CV(g_RQMutex); // request queue CV
std::queue<FILTER_MESSAGE_ACCESS_REQUEST> g_RequestQueue;
std::atomic<bool> g_Running(true);
HWND g_hMainWnd = NULL;
HWND g_hProtectedList = NULL;
HINSTANCE g_hInstance = NULL;
HWND g_hTrustedList = NULL;
UINT g_TimeoutDuration = 15000; // 15 seconds
ULONG g_DefaultAction = RESPONSE_TYPE_ACCESS_DENIED;      // Deny by default
/* Main */

#if _DEBUG
int main() {
	g_hInstance = GetModuleHandle(nullptr);
#else
int WINAPI wWinMain(
	_In_ HINSTANCE hInstance,
	_In_opt_ HINSTANCE hPrevInstance,
	_In_ LPWSTR lpCmdLine,
	_In_ int nCmdShow)
{
	UNREFERENCED_PARAMETER(hPrevInstance);
	UNREFERENCED_PARAMETER(lpCmdLine);
	g_hInstance = hInstance;
#endif


	LOG("Main start");
	INITCOMMONCONTROLSEX iccex;
	initCommonControls(&iccex);
	LOG("initiated common controls");
	Settings settings;
	if (!LoadSettings(&settings)) {
		settings.timeoutDuration = g_TimeoutDuration;
		settings.defaultAction = g_DefaultAction;
	}
	LOG("loaded settings");
	if (!InitializeDriver()) {
		MessageBox(NULL, 
			L"Failed to connect to the driver. Make sure the driver is installed and running.",
			L"Error", MB_OK | MB_ICONERROR);
		return 1;
	}
	DriverConnProtector _dp;
	_dp.arm();
	LOG("initialized driver");
	HANDLE hThread = CreateThread(NULL, 0, MessageThread, NULL, 0, NULL);
	if (hThread == NULL) {
		MessageBox(NULL, L"Failed to create message thread.", L"Error", MB_OK | MB_ICONERROR);
		CleanupDriver();
		_dp.disarm();
		return 1;
	}
	HANDLE mThread = CreateThread(NULL, 0, RequestQHandler, NULL, 0, NULL);
	if (mThread == NULL) {
		MessageBox(NULL, L"Failed to create request handler thread.", L"Error", MB_OK | MB_ICONERROR);
		CloseHandle(hThread);
		CleanupDriver();
		_dp.disarm();
		return 1;
	}
	LOG("created message and request handler threads");

	DialogBox(g_hInstance, MAKEINTRESOURCE(IDD_MAIN), NULL, MainDlgProc);
	LOG("dialog ended");
	// Shutdown
	g_Running = false;
	g_RQ_CV.notify_all();
	if (mThread != NULL) {
		LOG("waiting to close request handler thread");
		WaitForSingleObject(mThread, INFINITE);
		CloseHandle(mThread);
	}
	if (hThread != NULL) {
		LOG("waiting to close message thread");
		//WaitForSingleObject(hThread, INFINITE);
		CloseHandle(hThread);
	}
	CleanupDriver();
	_dp.disarm();
	LOG("Main end");
	return 0;
}