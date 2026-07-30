#include "WindowsService.h"

#if defined(ARCH_PORTDUINO) && defined(_WIN32)

#include "configuration.h"
#include "main.h"

#include <windows.h>

#include <cstdlib>
#include <thread>

// SERVICE_WIN32_OWN_PROCESS ignores the name, but the SCM still wants a non-null entry.
static const wchar_t *serviceName = L"meshtasticd";

// Re-reported while setup() runs and again while the shutdown path saves state. The SCM
// only enforces it against the checkpoint counter, so a generous hint costs nothing.
static const DWORD PENDING_WAIT_HINT_MS = 15000;

static SERVICE_STATUS_HANDLE statusHandle = nullptr;
static SERVICE_STATUS serviceStatus = {};
static DWORD checkPoint = 1;
static HANDLE readyEvent = nullptr;

static void reportStatus(DWORD state, DWORD waitHintMs)
{
    if (!statusHandle)
        return;

    serviceStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    serviceStatus.dwCurrentState = state;
    serviceStatus.dwControlsAccepted = (state == SERVICE_RUNNING) ? (SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN) : 0;
    serviceStatus.dwWin32ExitCode = NO_ERROR;
    serviceStatus.dwServiceSpecificExitCode = 0;
    serviceStatus.dwWaitHint = waitHintMs;
    // A stale checkpoint on a settled state makes the SCM think the transition hung.
    serviceStatus.dwCheckPoint = (state == SERVICE_RUNNING || state == SERVICE_STOPPED) ? 0 : checkPoint++;
    SetServiceStatus(statusHandle, &serviceStatus);
}

static void reportStopped()
{
    reportStatus(SERVICE_STOPPED, 0);
}

static DWORD WINAPI controlHandler(DWORD control, DWORD, LPVOID, LPVOID)
{
    switch (control) {
    case SERVICE_CONTROL_STOP:
    case SERVICE_CONTROL_SHUTDOWN:
        reportStatus(SERVICE_STOP_PENDING, PENDING_WAIT_HINT_MS);
        // Hand the teardown to the main thread rather than tearing down from this one:
        // powerCommandsCheck() runs the usual saveToDisk() and exits, and the atexit hook
        // registered below reports SERVICE_STOPPED on the way out.
        shutdownAtMsec = millis();
        return NO_ERROR;
    case SERVICE_CONTROL_INTERROGATE:
        return NO_ERROR;
    default:
        return ERROR_CALL_NOT_IMPLEMENTED;
    }
}

static void WINAPI serviceMain(DWORD, LPWSTR *)
{
    statusHandle = RegisterServiceCtrlHandlerExW(serviceName, controlHandler, nullptr);
    if (!statusHandle)
        return;

    // A cold node DB can push setup() past the SCM's 30 s start timeout, so keep the
    // transition alive with a fresh checkpoint until setup() signals ready.
    reportStatus(SERVICE_START_PENDING, PENDING_WAIT_HINT_MS);
    while (WaitForSingleObject(readyEvent, 5000) == WAIT_TIMEOUT)
        reportStatus(SERVICE_START_PENDING, PENDING_WAIT_HINT_MS);
    reportStatus(SERVICE_RUNNING, 0);

    // Returning would tell the SCM the service stopped while the node is still running.
    // The process ends from exit() on the main thread instead.
    Sleep(INFINITE);
}

void windowsServiceInit()
{
    readyEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!readyEvent) {
        LOG_ERROR("Service: CreateEvent failed (%lu), continuing in the foreground", GetLastError());
        return;
    }
    atexit(reportStopped);

    std::thread([] {
        SERVICE_TABLE_ENTRYW table[] = {{const_cast<LPWSTR>(serviceName), serviceMain}, {nullptr, nullptr}};
        // Fails immediately with ERROR_FAILED_SERVICE_CONTROLLER_CONNECT when --service was
        // typed at a console instead of coming from the SCM. Not fatal: the node runs anyway.
        if (!StartServiceCtrlDispatcherW(table))
            LOG_WARN("Service: not started by the SCM (%lu), running in the foreground", GetLastError());
    }).detach();
}

void windowsServiceReportRunning()
{
    if (readyEvent)
        SetEvent(readyEvent);
}

#endif
