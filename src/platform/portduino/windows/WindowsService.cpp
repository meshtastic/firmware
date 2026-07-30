#include "WindowsService.h"

#if defined(ARCH_PORTDUINO) && defined(_WIN32)

#include "configuration.h"
#include "main.h"

#include <windows.h>

#include <atomic>
#include <cstdlib>
#include <mutex>
#include <thread>

// SERVICE_WIN32_OWN_PROCESS ignores the name, but the SCM still wants a non-null entry.
static const wchar_t *serviceName = L"meshtasticd";

// Re-reported while setup() runs and again while the shutdown path saves state. The SCM
// only enforces it against the checkpoint counter, so a generous hint costs nothing.
static const DWORD PENDING_WAIT_HINT_MS = 15000;

// The SCM calls the control handler on its own thread, so a stop landing during the
// START_PENDING poll races the startup reports. statusMutex orders them.
static std::atomic<SERVICE_STATUS_HANDLE> statusHandle{nullptr};
static std::atomic<bool> stopping{false};
static std::mutex statusMutex;
static DWORD checkPoint = 1;
static HANDLE readyEvent = nullptr;

static void reportStatus(DWORD state, DWORD waitHintMs)
{
    SERVICE_STATUS_HANDLE handle = statusHandle.load();
    if (!handle)
        return;

    std::lock_guard<std::mutex> lock(statusMutex);
    // Latch the stop so a startup report queued behind it cannot walk the state back.
    if (state == SERVICE_STOP_PENDING || state == SERVICE_STOPPED)
        stopping.store(true);
    else if (stopping.load())
        return;

    SERVICE_STATUS status = {};
    status.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    status.dwCurrentState = state;
    status.dwControlsAccepted = (state == SERVICE_RUNNING) ? (SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN) : 0;
    status.dwWin32ExitCode = NO_ERROR;
    status.dwServiceSpecificExitCode = 0;
    status.dwWaitHint = waitHintMs;
    // A stale checkpoint on a settled state makes the SCM think the transition hung.
    status.dwCheckPoint = (state == SERVICE_RUNNING || state == SERVICE_STOPPED) ? 0 : checkPoint++;
    SetServiceStatus(handle, &status);
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
        // Teardown belongs on the main thread: powerCommandsCheck() saves and exits, and the
        // atexit hook reports SERVICE_STOPPED on the way out.
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
    statusHandle.store(RegisterServiceCtrlHandlerExW(serviceName, controlHandler, nullptr));
    if (!statusHandle.load())
        return;

    // A cold node DB can push setup() past the SCM's 30 s start timeout, so keep the
    // transition alive with a fresh checkpoint until setup() signals ready.
    reportStatus(SERVICE_START_PENDING, PENDING_WAIT_HINT_MS);
    while (!stopping.load() && WaitForSingleObject(readyEvent, 5000) == WAIT_TIMEOUT)
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
