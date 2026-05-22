#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#define NTDDI_VERSION   NTDDI_VISTA
#define _WIN32_WINNT    _WIN32_WINNT_VISTA

#include <windows.h>
#include <shellapi.h>
#include <tchar.h>
#include <string.h>

#include "resource.h"

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------
static const TCHAR* MUTEX_NAME = _T("Local\\TrayApp_{7A3B9F2E-1D4C-4E5A-8F6B-0C2D3E4F5A6B}");
static const TCHAR* WND_CLASS = _T("TrayAppWindowClass");
static const TCHAR* APP_TITLE = _T("TrayApp");
static const TCHAR* SERVICE_NAME = _T("TrayAppService");

// ---------------------------------------------------------------------------
// Globals
// ---------------------------------------------------------------------------
static HINSTANCE      g_hInst = nullptr;
static HWND           g_hWnd = nullptr;
static NOTIFYICONDATA g_nid = {};
static HANDLE         g_hMutex = nullptr;
static UINT           WM_TASKBAR_CREATED = 0;

// ---------------------------------------------------------------------------
// Forward declarations
// ---------------------------------------------------------------------------
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
static void      AddTrayIcon(HWND hWnd);
static void      RemoveTrayIcon();
static void      ShowTrayContextMenu(HWND hWnd);
static HMENU     CreateMainMenu();
static void      ShowMainWindow();
static void      ExitApp();
static void      EnsureServiceRunning();

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------
int WINAPI wWinMain(_In_ HINSTANCE hInstance,
    _In_opt_ HINSTANCE /*hPrevInstance*/,
    _In_ LPWSTR lpCmdLine,
    _In_ int nCmdShow)
{
    // ---- Requirement 10: single-instance per user via named mutex ----------
    g_hMutex = CreateMutex(nullptr, TRUE, MUTEX_NAME);
    if (!g_hMutex || GetLastError() == ERROR_ALREADY_EXISTS) {
        if (g_hMutex) CloseHandle(g_hMutex);
        return 0;   // exit before any UI / tray icon is created
    }

    g_hInst = hInstance;

    // ---- Check and start the service if needed -----------------------------
    EnsureServiceRunning();

    // Register message for taskbar recreation (Requirement 6)
    WM_TASKBAR_CREATED = RegisterWindowMessage(_T("TaskbarCreated"));

    // ---- Register window class ---------------------------------------------
    WNDCLASSEX wcex = {};
    wcex.cbSize = sizeof(wcex);
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = WndProc;
    wcex.hInstance = hInstance;
    wcex.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wcex.lpszClassName = WND_CLASS;
    wcex.hIconSm = LoadIcon(nullptr, IDI_APPLICATION);
    if (!RegisterClassEx(&wcex)) {
        ReleaseMutex(g_hMutex);
        CloseHandle(g_hMutex);
        return 0;
    }

    // ---- Create main window ------------------------------------------------
    g_hWnd = CreateWindowEx(
        0, WND_CLASS, APP_TITLE,
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 640, 480,
        nullptr, CreateMainMenu(), hInstance, nullptr);

    if (!g_hWnd) {
        ReleaseMutex(g_hMutex);
        CloseHandle(g_hMutex);
        return 0;
    }

    // ---- Requirement 1: add tray icon on startup ---------------------------
    AddTrayIcon(g_hWnd);

    // ---- Requirement 7: support hidden-start mode --------------------------
    bool startHidden = false;
    if (lpCmdLine && (wcsstr(lpCmdLine, L"--hidden") ||
        wcsstr(lpCmdLine, L"/hidden"))) {
        startHidden = true;
    }

    if (!startHidden) {
        ShowWindow(g_hWnd, nCmdShow);
        UpdateWindow(g_hWnd);
    }

    // ---- Message loop ------------------------------------------------------
    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    RemoveTrayIcon();
    ReleaseMutex(g_hMutex);
    CloseHandle(g_hMutex);
    return static_cast<int>(msg.wParam);
}

// ---------------------------------------------------------------------------
// Requirement 9: main window menu  File -> Exit
// ---------------------------------------------------------------------------
static HMENU CreateMainMenu()
{
    HMENU hMenu = ::CreateMenu();
    HMENU hFileMenu = ::CreatePopupMenu();
    AppendMenu(hFileMenu, MF_STRING, IDM_FILE_EXIT, _T("Выход"));
    AppendMenu(hMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(hFileMenu), _T("Файл"));
    return hMenu;
}

// ---------------------------------------------------------------------------
// Requirement 1 & 6: tray icon management
// ---------------------------------------------------------------------------
static void AddTrayIcon(HWND hWnd)
{
    g_nid.cbSize = sizeof(g_nid);
    g_nid.hWnd = hWnd;
    g_nid.uID = IDI_TRAYAPP;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAYICON;
    g_nid.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    lstrcpyn(g_nid.szTip, APP_TITLE, ARRAYSIZE(g_nid.szTip));
    Shell_NotifyIcon(NIM_ADD, &g_nid);
}

static void RemoveTrayIcon()
{
    Shell_NotifyIcon(NIM_DELETE, &g_nid);
}

// ---------------------------------------------------------------------------
// Requirements 3-5: tray context menu
// ---------------------------------------------------------------------------
static void ShowTrayContextMenu(HWND hWnd)
{
    POINT pt;
    GetCursorPos(&pt);

    HMENU hMenu = CreatePopupMenu();
    AppendMenu(hMenu, MF_STRING, IDM_TRAY_OPEN, _T("Открыть"));
    AppendMenu(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenu(hMenu, MF_STRING, IDM_TRAY_EXIT, _T("Выход"));

    // Required so the menu dismisses when clicking elsewhere
    SetForegroundWindow(hWnd);
    TrackPopupMenu(hMenu, TPM_BOTTOMALIGN | TPM_LEFTALIGN,
        pt.x, pt.y, 0, hWnd, nullptr);
    PostMessage(hWnd, WM_NULL, 0, 0);

    DestroyMenu(hMenu);
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
static void ShowMainWindow()
{
    ShowWindow(g_hWnd, SW_SHOW);
    ShowWindow(g_hWnd, SW_RESTORE);
    SetForegroundWindow(g_hWnd);
}

static void ExitApp()
{
    DestroyWindow(g_hWnd);
}

// ---------------------------------------------------------------------------
// Check service status and start it if stopped
// ---------------------------------------------------------------------------
static void EnsureServiceRunning()
{
    SC_HANDLE hSCM = OpenSCManager(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!hSCM) return;

    SC_HANDLE hSvc = OpenService(hSCM, SERVICE_NAME,
        SERVICE_QUERY_STATUS | SERVICE_START);
    if (hSvc) {
        SERVICE_STATUS ss = {};
        if (QueryServiceStatus(hSvc, &ss)) {
            if (ss.dwCurrentState == SERVICE_STOPPED) {
                StartService(hSvc, 0, nullptr);
            }
        }
        CloseServiceHandle(hSvc);
    }
    CloseServiceHandle(hSCM);
}

// ---------------------------------------------------------------------------
// Window procedure
// ---------------------------------------------------------------------------
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    // Requirement 6: re-add tray icon when taskbar is recreated
    if (message == WM_TASKBAR_CREATED && WM_TASKBAR_CREATED != 0) {
        AddTrayIcon(hWnd);
        return 0;
    }

    switch (message) {

        // ---- Tray icon events (Requirements 2-5) -------------------------------
    case WM_TRAYICON:
        switch (lParam) {
        case WM_LBUTTONUP:              // Requirement 2: left-click -> show
            ShowMainWindow();
            break;
        case WM_RBUTTONUP:              // Requirement 3: right-click -> menu
            ShowTrayContextMenu(hWnd);
            break;
        }
        break;

        // ---- Menu commands -----------------------------------------------------
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDM_FILE_EXIT:             // Requirement 9
        case IDM_TRAY_EXIT:             // Requirement 5
            ExitApp();
            break;
        case IDM_TRAY_OPEN:             // Requirement 4
            ShowMainWindow();
            break;
        }
        break;

        // ---- Requirement 8: close hides, not exits -----------------------------
    case WM_CLOSE:
        ShowWindow(hWnd, SW_HIDE);
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        break;

    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}