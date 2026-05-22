#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#define NTDDI_VERSION   NTDDI_VISTA
#define _WIN32_WINNT    _WIN32_WINNT_VISTA

#include <windows.h>
#include <wtsapi32.h>
#include <userenv.h>
#include <tchar.h>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------
static const TCHAR* SERVICE_NAME = _T("TrayAppService");
static const TCHAR* SERVICE_DISPLAY = _T("TrayApp Background Service");
static const TCHAR* TRAYAPP_EXE = _T("TrayApp.exe");

// ---------------------------------------------------------------------------
// Globals
// ---------------------------------------------------------------------------
static SERVICE_STATUS        g_svcStatus = {};
static SERVICE_STATUS_HANDLE g_svcStatusHandle = nullptr;
static HANDLE                g_hStopEvent = nullptr;

// ---------------------------------------------------------------------------
// Forward declarations
// ---------------------------------------------------------------------------
static void WINAPI ServiceMain(DWORD argc, LPTSTR* argv);
static DWORD WINAPI ServiceCtrlHandler(DWORD dwControl, DWORD dwEventType,
    LPVOID lpEventData, LPVOID lpContext);
static void SetServiceStatus(DWORD state, DWORD exitCode = 0);
static void LaunchTrayAppInSession(DWORD sessionId);
static void LaunchTrayAppInAllSessions();
static std::wstring GetTrayAppPath();

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------
int wmain(int argc, wchar_t* argv[])
{
    SERVICE_TABLE_ENTRY dispatchTable[] = {
        { const_cast<LPTSTR>(SERVICE_NAME), ServiceMain },
        { nullptr, nullptr }
    };

    if (!StartServiceCtrlDispatcher(dispatchTable)) {
        // If not started as a service, allow install/uninstall via command line
        if (argc >= 2) {
            if (_wcsicmp(argv[1], L"install") == 0) {
                // Install the service
                SC_HANDLE hSCM = OpenSCManager(nullptr, nullptr, SC_MANAGER_CREATE_SERVICE);
                if (!hSCM) return 1;

                wchar_t modulePath[MAX_PATH];
                GetModuleFileName(nullptr, modulePath, MAX_PATH);

                SC_HANDLE hSvc = CreateService(
                    hSCM, SERVICE_NAME, SERVICE_DISPLAY,
                    SERVICE_ALL_ACCESS, SERVICE_WIN32_OWN_PROCESS,
                    SERVICE_AUTO_START, SERVICE_ERROR_NORMAL,
                    modulePath, nullptr, nullptr, nullptr, nullptr, nullptr);

                if (hSvc) {
                    // Set description
                    SERVICE_DESCRIPTION desc;
                    desc.lpDescription = const_cast<LPWSTR>(
                        L"Launches TrayApp in user sessions automatically.");
                    ChangeServiceConfig2(hSvc, SERVICE_CONFIG_DESCRIPTION, &desc);
                    CloseServiceHandle(hSvc);
                    wprintf(L"Service installed successfully.\n");
                }
                else {
                    wprintf(L"Failed to install service: %lu\n", GetLastError());
                }
                CloseServiceHandle(hSCM);
            }
            else if (_wcsicmp(argv[1], L"uninstall") == 0) {
                SC_HANDLE hSCM = OpenSCManager(nullptr, nullptr, SC_MANAGER_ALL_ACCESS);
                if (!hSCM) return 1;
                SC_HANDLE hSvc = OpenService(hSCM, SERVICE_NAME, DELETE | SERVICE_STOP);
                if (hSvc) {
                    SERVICE_STATUS ss;
                    ControlService(hSvc, SERVICE_CONTROL_STOP, &ss);
                    if (DeleteService(hSvc))
                        wprintf(L"Service uninstalled successfully.\n");
                    else
                        wprintf(L"Failed to uninstall: %lu\n", GetLastError());
                    CloseServiceHandle(hSvc);
                }
                CloseServiceHandle(hSCM);
            }
        }
    }
    return 0;
}

// ---------------------------------------------------------------------------
// ServiceMain
// ---------------------------------------------------------------------------
static void WINAPI ServiceMain(DWORD /*argc*/, LPTSTR* /*argv*/)
{
    g_svcStatusHandle = RegisterServiceCtrlHandlerEx(
        SERVICE_NAME, ServiceCtrlHandler, nullptr);
    if (!g_svcStatusHandle) return;

    SetServiceStatus(SERVICE_START_PENDING);

    g_hStopEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
    if (!g_hStopEvent) {
        SetServiceStatus(SERVICE_STOPPED, GetLastError());
        return;
    }

    SetServiceStatus(SERVICE_RUNNING);

    // Launch TrayApp in all existing user sessions
    LaunchTrayAppInAllSessions();

    // Wait until service is signalled to stop
    WaitForSingleObject(g_hStopEvent, INFINITE);

    CloseHandle(g_hStopEvent);
    SetServiceStatus(SERVICE_STOPPED);
}

// ---------------------------------------------------------------------------
// Service control handler — handles stop + session change
// ---------------------------------------------------------------------------
static DWORD WINAPI ServiceCtrlHandler(DWORD dwControl, DWORD dwEventType,
    LPVOID /*lpEventData*/, LPVOID /*lpContext*/)
{
    switch (dwControl) {
    case SERVICE_CONTROL_STOP:
        SetServiceStatus(SERVICE_STOP_PENDING);
        SetEvent(g_hStopEvent);
        return NO_ERROR;

    case SERVICE_CONTROL_SESSIONCHANGE:
        // New session logon — launch TrayApp there
        if (dwEventType == WTS_SESSION_LOGON) {
            PWTSSESSION_NOTIFICATION pNotify =
                reinterpret_cast<PWTSSESSION_NOTIFICATION>(
                    const_cast<LPVOID>(
                        reinterpret_cast<const void*>(&dwEventType)));
            // Safer: get session id from the struct passed via lpEventData
            // But we re-enumerate all active sessions for reliability
            LaunchTrayAppInAllSessions();
        }
        return NO_ERROR;

    case SERVICE_CONTROL_INTERROGATE:
        return NO_ERROR;

    default:
        return ERROR_CALL_NOT_IMPLEMENTED;
    }
}

// ---------------------------------------------------------------------------
// Set service status helper
// ---------------------------------------------------------------------------
static void SetServiceStatus(DWORD state, DWORD exitCode)
{
    g_svcStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    g_svcStatus.dwCurrentState = state;
    g_svcStatus.dwWin32ExitCode = exitCode;
    g_svcStatus.dwControlsAccepted = (state == SERVICE_RUNNING)
        ? (SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SESSIONCHANGE)
        : 0;
    g_svcStatus.dwCheckPoint = 0;
    g_svcStatus.dwWaitHint = 0;
    ::SetServiceStatus(g_svcStatusHandle, &g_svcStatus);
}

// ---------------------------------------------------------------------------
// Get path to TrayApp.exe (same directory as service exe)
// ---------------------------------------------------------------------------
static std::wstring GetTrayAppPath()
{
    wchar_t modulePath[MAX_PATH];
    GetModuleFileName(nullptr, modulePath, MAX_PATH);
    std::wstring path(modulePath);
    auto pos = path.find_last_of(L'\\');
    if (pos != std::wstring::npos)
        path = path.substr(0, pos + 1);
    path += TRAYAPP_EXE;
    return path;
}

// ---------------------------------------------------------------------------
// Launch TrayApp.exe in a specific user session (with --hidden)
// ---------------------------------------------------------------------------
static void LaunchTrayAppInSession(DWORD sessionId)
{
    HANDLE hToken = nullptr;
    if (!WTSQueryUserToken(sessionId, &hToken))
        return;

    HANDLE hDupToken = nullptr;
    if (!DuplicateTokenEx(hToken, MAXIMUM_ALLOWED, nullptr,
        SecurityIdentification, TokenPrimary, &hDupToken)) {
        CloseHandle(hToken);
        return;
    }

    LPVOID pEnv = nullptr;
    CreateEnvironmentBlock(&pEnv, hDupToken, FALSE);

    std::wstring appPath = GetTrayAppPath();
    std::wstring cmdLine = L"\"" + appPath + L"\" --hidden";

    // Need a writable buffer for CreateProcessAsUser
    std::vector<wchar_t> cmdBuf(cmdLine.begin(), cmdLine.end());
    cmdBuf.push_back(L'\0');

    STARTUPINFO si = {};
    si.cb = sizeof(si);
    si.lpDesktop = const_cast<LPWSTR>(L"winsta0\\default");
    PROCESS_INFORMATION pi = {};

    CreateProcessAsUser(
        hDupToken,
        appPath.c_str(),
        cmdBuf.data(),
        nullptr, nullptr, FALSE,
        CREATE_UNICODE_ENVIRONMENT | CREATE_NO_WINDOW,
        pEnv, nullptr, &si, &pi);

    if (pi.hProcess) CloseHandle(pi.hProcess);
    if (pi.hThread)  CloseHandle(pi.hThread);
    if (pEnv)        DestroyEnvironmentBlock(pEnv);
    CloseHandle(hDupToken);
    CloseHandle(hToken);
}

// ---------------------------------------------------------------------------
// Enumerate all active user sessions and launch TrayApp in each
// ---------------------------------------------------------------------------
static void LaunchTrayAppInAllSessions()
{
    PWTS_SESSION_INFO pSessions = nullptr;
    DWORD count = 0;

    if (!WTSEnumerateSessions(WTS_CURRENT_SERVER_HANDLE, 0, 1, &pSessions, &count))
        return;

    for (DWORD i = 0; i < count; ++i) {
        if (pSessions[i].State == WTSActive) {
            LaunchTrayAppInSession(pSessions[i].SessionId);
        }
    }

    WTSFreeMemory(pSessions);
}