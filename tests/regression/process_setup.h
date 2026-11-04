#ifndef CHAPEAU_REGRESSION_PROCESS_SETUP_H
#define CHAPEAU_REGRESSION_PROCESS_SETUP_H

// Private to the subprocess regression harness (tests/regression); not installed, not used by Chapeau itself.
//
// chapeau_harness::prepareProcess() is the first thing main() of every harness program does.
//
// On Windows (MSVC) it prepares the process so that however a scenario ends, nothing waits for a person and RunCase.cmake can
// tell how it ended:
//
//  * SetErrorMode: no Windows Error Reporting, JIT-debugger or "critical error" dialog for this process.
//  * Debug CRT: assertion, error and warning reports go to stderr instead of a message box. A message box would hold the
//    scenario until its time limit.
//  * _set_abort_behavior(_CALL_REPORTFAULT): abort() ends the process in a fast fail, status 0xC0000409, in Debug as well as in
//    Release. Without it a Debug abort() waits on a message box, or (with dialogs off) ends with the plain exit status 3, which
//    is the same as a program that returns 3.
//  * A SIGABRT handler writes the marker line below to stderr, flushes, and returns, so abort() carries on and ends the process
//    exactly as it would have. Status 0xC0000409 is not unique to abort() (a failed stack check, __fastfail, std::terminate and
//    a program that simply returns it all give it); the marker is what shows that the process really went through abort().
//    RunCase.cmake requires it, together with the status and the expected Chapeau diagnostic, for EXPECT=abort on Windows.
//
// Deliberately NOT done: an unhandled-exception filter. Installing one replaces the CRT's own, and an uncaught C++ exception then
// no longer ends in std::terminate()/abort().
//
// On every other platform prepareProcess() does nothing: signal handling and process termination are untouched.

#if defined(_WIN32) && defined(_MSC_VER)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <crtdbg.h>
#include <csignal>
#include <cstdio>
#include <cstdlib>

namespace chapeau_harness {

// The text RunCase.cmake looks for (keep the two in step). One line, on its own.
inline const char* sigabrtMarker() {
   return "CHAPEAU-HARNESS: SIGABRT";
}

inline void onSigabrt(int) {
   std::fprintf(stderr, "\n%s\n", sigabrtMarker());
   std::fflush(stderr);
   std::fflush(stdout);
   // returns: abort() then ends the process as it would have without this handler
}

inline void prepareProcess() {
   SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
#ifdef _DEBUG
   _CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_FILE);
   _CrtSetReportFile(_CRT_WARN, _CRTDBG_FILE_STDERR);
   _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
   _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
   _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
   _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
#endif
   _set_abort_behavior(_CALL_REPORTFAULT, _CALL_REPORTFAULT);
   std::signal(SIGABRT, onSigabrt);
}

}

#else

namespace chapeau_harness {

inline void prepareProcess() {
}

}

#endif

#endif
