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
int WINAPI wWinMain(HINSTANCE hInstance,
    HINSTANCE,
    LPWSTR lpCmdLine,
    int nCmdShow)
{
    g_hMutex = CreateMutex(nullptr, TRUE, MUTEX_NAME);
    if (!g_hMutex || GetLastError() == ERROR_ALREADY_EXISTS) {
        if (g_hMutex) CloseHandle(g_hMutex);
        return 0;
    }

    g_hInst = hInstance;

    EnsureServiceRunning();

    WM_TASKBAR_CREATED = RegisterWindowMessage(_T("TaskbarCreated"));

    WNDCLASSEX wcex = {};
    wcex.cbSize = sizeof(wcex);
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = WndProc;
    wcex.hInstance = hInstance;
    wcex.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wcex.lpszClassName = WND_CLASS;
    wcex.hIconSm = LoadIcon(nullptr, IDI_APPLICATION);

    if (!RegisterClassEx(&wcex)) {
        ReleaseMutex(g_hMutex);
        CloseHandle(g_hMutex);
        return 0;
    }

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

    AddTrayIcon(g_hWnd);

    bool startHidden = false;
    if (lpCmdLine && (wcsstr(lpCmdLine, L"--hidden") ||
        wcsstr(lpCmdLine, L"/hidden"))) {
        startHidden = true;
    }

    if (!startHidden) {
        ShowWindow(g_hWnd, nCmdShow);
        UpdateWindow(g_hWnd);
    }

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    RemoveTrayIcon();
    ReleaseMutex(g_hMutex);
    CloseHandle(g_hMutex);
    return (int)msg.wParam;
}

// ---------------------------------------------------------------------------
// Menu
// ---------------------------------------------------------------------------
static HMENU CreateMainMenu()
{
    HMENU hMenu = CreateMenu();
    HMENU hFile = CreatePopupMenu();

    AppendMenu(hFile, MF_STRING, IDM_FILE_EXIT, _T("Exit"));
    AppendMenu(hMenu, MF_POPUP, (UINT_PTR)hFile, _T("File"));

    return hMenu;
}

// ---------------------------------------------------------------------------
// Tray icon
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
// Tray menu
// ---------------------------------------------------------------------------
static void ShowTrayContextMenu(HWND hWnd)
{
    POINT pt;
    GetCursorPos(&pt);

    HMENU hMenu = CreatePopupMenu();
    AppendMenu(hMenu, MF_STRING, IDM_TRAY_OPEN, _T("Open"));
    AppendMenu(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenu(hMenu, MF_STRING, IDM_TRAY_EXIT, _T("Exit"));

    SetForegroundWindow(hWnd);
    TrackPopupMenu(hMenu, TPM_BOTTOMALIGN | TPM_LEFTALIGN,
        pt.x, pt.y, 0, hWnd, nullptr);

    PostMessage(hWnd, WM_NULL, 0, 0);
    DestroyMenu(hMenu);
}

// ---------------------------------------------------------------------------
static void ShowMainWindow()
{
    ShowWindow(g_hWnd, SW_SHOW);
    SetForegroundWindow(g_hWnd);
}

static void ExitApp()
{
    DestroyWindow(g_hWnd);
}

// ---------------------------------------------------------------------------
// Service stub (unchanged logic)
// ---------------------------------------------------------------------------
static void EnsureServiceRunning()
{
    // unchanged from your version
}

// ---------------------------------------------------------------------------
// Window procedure
// ---------------------------------------------------------------------------
LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == WM_TASKBAR_CREATED) {
        AddTrayIcon(hWnd);
        return 0;
    }

    switch (msg)
    {
    case WM_TRAYICON:
        if (lParam == WM_LBUTTONUP)
            ShowMainWindow();
        else if (lParam == WM_RBUTTONUP)
            ShowTrayContextMenu(hWnd);
        break;

    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case IDM_FILE_EXIT:
        case IDM_TRAY_EXIT:
            ExitApp();
            break;
        case IDM_TRAY_OPEN:
            ShowMainWindow();
            break;
        }
        break;

    case WM_CLOSE:
        ShowWindow(hWnd, SW_HIDE);
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        break;

    default:
        return DefWindowProc(hWnd, msg, wParam, lParam);
    }

    return 0;
}