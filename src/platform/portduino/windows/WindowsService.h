#pragma once

#if defined(ARCH_PORTDUINO) && defined(_WIN32)

// Connects the process to the Service Control Manager. Called from parse_opt() for --service,
// which is how the MSI-registered ImagePath starts meshtasticd.
void windowsServiceInit();

// Moves the SCM status from START_PENDING to RUNNING; called once setup() has returned.
void windowsServiceReportRunning();

#else

inline void windowsServiceInit() {}
inline void windowsServiceReportRunning() {}

#endif
