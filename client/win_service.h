// win_service.h — Windows Service wrapper for NetCommand Client
// Provides SCM (Service Control Manager) integration so the client
// starts automatically at boot, before any user logs in.
//
// Usage:
//   netcommand-client.exe --install   <server-ip> [port]
//   netcommand-client.exe --uninstall
//   netcommand-client.exe --run       <server-ip> [port]  (SCM starts this)
//   netcommand-client.exe <server-ip> [port]              (direct / debug)
#pragma once
#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string>
#include <functional>

#define SVC_NAME  L"NetCommandClient"
#define SVC_DISPLAY L"NetCommand Client"
#define SVC_DESC  L"NetCommand remote management client daemon"

// Callback type: the real main logic
using ServiceMainFn = std::function<void(const std::string& server_ip, int port)>;

// Global so SCM callbacks can reach it
static ServiceMainFn   g_svc_main_fn;
static SERVICE_STATUS  g_svc_status;
static SERVICE_STATUS_HANDLE g_svc_handle = nullptr;
static HANDLE          g_svc_stop_event   = nullptr;
static std::string     g_svc_server_ip;
static int             g_svc_port         = 7890;

// ── SCM control handler ───────────────────────────────
static VOID WINAPI SvcCtrlHandler(DWORD ctrl)
{
    switch (ctrl) {
    case SERVICE_CONTROL_STOP:
    case SERVICE_CONTROL_SHUTDOWN:
        g_svc_status.dwCurrentState = SERVICE_STOP_PENDING;
        SetServiceStatus(g_svc_handle, &g_svc_status);
        SetEvent(g_svc_stop_event);
        break;
    default: break;
    }
}

// ── SCM service main ──────────────────────────────────
static VOID WINAPI SvcMain(DWORD argc, LPWSTR* argv)
{
    g_svc_handle = RegisterServiceCtrlHandlerW(SVC_NAME, SvcCtrlHandler);
    if (!g_svc_handle) return;

    g_svc_status.dwServiceType             = SERVICE_WIN32_OWN_PROCESS;
    g_svc_status.dwCurrentState            = SERVICE_START_PENDING;
    g_svc_status.dwControlsAccepted        = SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN;
    g_svc_status.dwWin32ExitCode           = 0;
    g_svc_status.dwServiceSpecificExitCode = 0;
    g_svc_status.dwCheckPoint              = 0;
    g_svc_status.dwWaitHint                = 3000;
    SetServiceStatus(g_svc_handle, &g_svc_status);

    g_svc_stop_event = CreateEvent(nullptr, TRUE, FALSE, nullptr);

    g_svc_status.dwCurrentState = SERVICE_RUNNING;
    g_svc_status.dwCheckPoint   = 0;
    g_svc_status.dwWaitHint     = 0;
    SetServiceStatus(g_svc_handle, &g_svc_status);

    // Run the actual client logic in a separate thread
    HANDLE h = CreateThread(nullptr, 0, [](LPVOID) -> DWORD {
        if (g_svc_main_fn) g_svc_main_fn(g_svc_server_ip, g_svc_port);
        return 0;
    }, nullptr, 0, nullptr);

    WaitForSingleObject(g_svc_stop_event, INFINITE);

    // Signal client loop to stop (extern from main.cpp)
    extern std::atomic<bool> g_running;
    g_running = false;

    WaitForSingleObject(h, 5000);
    CloseHandle(h);
    CloseHandle(g_svc_stop_event);

    g_svc_status.dwCurrentState = SERVICE_STOPPED;
    SetServiceStatus(g_svc_handle, &g_svc_status);
}

// ── Install as Windows Service ────────────────────────
static bool svc_install(const std::string& server_ip, int port)
{
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(nullptr, path, MAX_PATH);

    // Build binary path with arguments
    std::wstring bin_path = std::wstring(L"\"") + path + L"\" --run ";
    bin_path += std::wstring(server_ip.begin(), server_ip.end());
    bin_path += L" " + std::to_wstring(port);

    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CREATE_SERVICE);
    if (!scm) { fprintf(stderr, "[install] OpenSCManager failed: %lu\n", GetLastError()); return false; }

    SC_HANDLE svc = CreateServiceW(
        scm, SVC_NAME, SVC_DISPLAY,
        SERVICE_ALL_ACCESS,
        SERVICE_WIN32_OWN_PROCESS,
        SERVICE_AUTO_START,          // start at boot
        SERVICE_ERROR_NORMAL,
        bin_path.c_str(),
        nullptr, nullptr, nullptr, nullptr, nullptr);

    if (!svc) {
        DWORD err = GetLastError();
        CloseServiceHandle(scm);
        if (err == ERROR_SERVICE_EXISTS) {
            fprintf(stderr, "[install] Service already exists. Use --uninstall first.\n");
        } else {
            fprintf(stderr, "[install] CreateService failed: %lu\n", err);
        }
        return false;
    }

    // Set description
    SERVICE_DESCRIPTIONW desc;
    desc.lpDescription = (LPWSTR)SVC_DESC;
    ChangeServiceConfig2W(svc, SERVICE_CONFIG_DESCRIPTION, &desc);

    // Start immediately
    StartServiceW(svc, 0, nullptr);

    CloseServiceHandle(svc);
    CloseServiceHandle(scm);
    printf("[install] Service installed and started.\n");
    return true;
}

// ── Uninstall Windows Service ─────────────────────────
static bool svc_uninstall()
{
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_ALL_ACCESS);
    if (!scm) return false;

    SC_HANDLE svc = OpenServiceW(scm, SVC_NAME, SERVICE_STOP | DELETE);
    if (!svc) { CloseServiceHandle(scm); return false; }

    SERVICE_STATUS st;
    ControlService(svc, SERVICE_CONTROL_STOP, &st);
    Sleep(1000);

    DeleteService(svc);
    CloseServiceHandle(svc);
    CloseServiceHandle(scm);
    printf("[uninstall] Service removed.\n");
    return true;
}

// ── Entry point helper ────────────────────────────────
// Call from WinMain; returns true if handled by SCM (don't run normal main).
static bool svc_dispatch_or_handle(int argc, char* argv[], ServiceMainFn fn)
{
    g_svc_main_fn = fn;

    if (argc >= 2) {
        std::string mode = argv[1];

        if (mode == "--install" && argc >= 3) {
            g_svc_server_ip = argv[2];
            g_svc_port = (argc >= 4) ? atoi(argv[3]) : 7890;
            svc_install(g_svc_server_ip, g_svc_port);
            return true;
        }
        if (mode == "--uninstall") {
            svc_uninstall();
            return true;
        }
        if (mode == "--run" && argc >= 3) {
            g_svc_server_ip = argv[2];
            g_svc_port = (argc >= 4) ? atoi(argv[3]) : 7890;
            // Hand control to SCM
            SERVICE_TABLE_ENTRYW tbl[] = {
                { (LPWSTR)SVC_NAME, SvcMain },
                { nullptr, nullptr }
            };
            StartServiceCtrlDispatcherW(tbl);
            return true;
        }
    }
    return false; // Fall through to normal (debug) execution
}

#endif // _WIN32
