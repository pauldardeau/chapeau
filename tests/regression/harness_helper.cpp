// A tiny program whose only job is to end in every way the regression
// harness (RunCase.cmake) must tell apart. It does not use Chapeau.
//
//   harness_helper <scenario>
//
// ok                   prints a line, exits 0
// exit3                prints a line, exits 3
// abort                std::abort()
// segv                 dies from SIGSEGV with the default action (a sanitizer
//                      handler, if linked, is removed first so this stays a
//                      plain crash). On Windows raise(SIGSEGV) only ends the
//                      process with exit status 3, so there it is a real
//                      access violation instead
// abort_with_diagnostic  prints "helper fatal: reason X" on stderr, then std::abort()
// segv_with_diagnostic   prints the same line, then dies as segv does
// sanitizer_then_abort   prints an ASan-style report, then std::abort()
// hang                 never ends
// sanitizer_exit0      prints a UBSan-style report and exits 0
// sanitizer_exit1      prints an ASan-style report and exits 1
// sanitizer_fatal      prints a sanitizer start-up failure (not a finding) and exits 66
// leave_temp_file      leaves a file in its temporary directory, exits 0
// clean_temp_file      uses and removes a file in its temporary directory, exits 0
// exit255              prints a line, exits 255
// exit3_with_diagnostic  prints the diagnostic line, then exits 3
// hang_with_diagnostic   prints the diagnostic line, then never ends
// Windows only, for the self-check of the Windows rules (see SelfCheck.cmake):
// failfast_with_diagnostic  prints the diagnostic line, then __fastfail(): status 0xC0000409 without going through abort()
// return_failfast_status_with_diagnostic  prints the diagnostic line, then simply RETURNS 0xC0000409
// breakpoint           an unhandled breakpoint (CMake reports its status as a plain number)
// breakpoint_with_diagnostic  prints the diagnostic line, then an unhandled breakpoint
// seh_custom           an unhandled custom SEH exception, 0xE0000001 (also a plain number)
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <thread>
#include <chrono>
#if defined(_WIN32) && defined(_MSC_VER)
#include <intrin.h>
#endif

#include "process_setup.h"

namespace fs = std::filesystem;

namespace {

// the line the "with_diagnostic" scenarios print; the self-check asks the driver to look for it
void printDiagnostic() {
   std::fprintf(stderr, "helper fatal: reason X\n");
   std::fflush(stderr);
}

// A real, fatal fault. On Windows raise(SIGSEGV) is no fault at all: it ends the process with exit status 3, the same as a
// program that returns 3. So there the access violation is real.
void dieBySegv() {
#if defined(_WIN32) && defined(_MSC_VER)
   *static_cast<volatile int*>(nullptr) = 1;
#else
   std::signal(SIGSEGV, SIG_DFL);
   std::raise(SIGSEGV);
#endif
}

}

int main(int argc, char** argv) {
   chapeau_harness::prepareProcess();
   if (argc < 2) {
      std::fprintf(stderr, "usage: harness_helper <scenario>\n");
      return 2;
   }
   const char* scenario = argv[1];

   if (!std::strcmp(scenario, "ok")) {
      std::printf("helper says hello\n");
      return 0;
   }
   if (!std::strcmp(scenario, "exit3")) {
      std::printf("helper exits with 3\n");
      return 3;
   }
   if (!std::strcmp(scenario, "abort")) {
      std::fflush(stdout);
      std::abort();
   }
   if (!std::strcmp(scenario, "abort_with_diagnostic")) {
      std::fprintf(stderr, "helper fatal: reason X\n");
      std::fflush(stderr);
      std::abort();
   }
   if (!std::strcmp(scenario, "segv_with_diagnostic")) {
      std::fprintf(stderr, "helper fatal: reason X\n");
      std::fflush(stderr);
      dieBySegv();
      return 0;
   }
   if (!std::strcmp(scenario, "sanitizer_then_abort")) {
      std::fprintf(stderr, "==1==ERROR: AddressSanitizer: heap-use-after-free on address 0x1\nhelper fatal: reason X\n");
      std::fflush(stderr);
      std::abort();
   }
   if (!std::strcmp(scenario, "segv")) {
      std::fflush(stdout);
      dieBySegv();
      return 0;
   }
   if (!std::strcmp(scenario, "hang")) {
      std::printf("helper hangs\n");
      std::fflush(stdout);
      for (;;) {
         std::this_thread::sleep_for(std::chrono::seconds(1));
      }
   }
   if (!std::strcmp(scenario, "sanitizer_exit0")) {
      std::fprintf(stderr, "helper.cpp:1:1: runtime error: signed integer overflow\n");
      return 0;
   }
   if (!std::strcmp(scenario, "sanitizer_exit1")) {
      std::fprintf(stderr, "==1==ERROR: AddressSanitizer: heap-use-after-free on address 0x1\n");
      return 1;
   }
   if (!std::strcmp(scenario, "sanitizer_fatal")) {
      std::fprintf(stderr, "==1==FATAL: ThreadSanitizer: unexpected memory mapping 0x1\n");
      return 66;
   }
   if (!std::strcmp(scenario, "leave_temp_file")) {
      std::ofstream(fs::temp_directory_path() / "left-behind.txt") << "x";
      return 0;
   }
   if (!std::strcmp(scenario, "clean_temp_file")) {
      const fs::path file = fs::temp_directory_path() / "used.txt";
      std::ofstream(file) << "x";
      fs::remove(file);
      return 0;
   }
   if (!std::strcmp(scenario, "exit255")) {
      std::printf("helper exits with 255\n");
      return 255;
   }
   if (!std::strcmp(scenario, "exit3_with_diagnostic")) {
      printDiagnostic();
      return 3;
   }
   if (!std::strcmp(scenario, "hang_with_diagnostic")) {
      printDiagnostic();
      for (;;) {
         std::this_thread::sleep_for(std::chrono::seconds(1));
      }
   }
#if defined(_WIN32) && defined(_MSC_VER)
   if (!std::strcmp(scenario, "failfast_with_diagnostic")) {
      printDiagnostic();
      __fastfail(7);   // FAST_FAIL_FATAL_APP_EXIT: status 0xC0000409, but not through abort() and so no SIGABRT
   }
   if (!std::strcmp(scenario, "return_failfast_status_with_diagnostic")) {
      printDiagnostic();
      return static_cast<int>(0xC0000409u);
   }
   if (!std::strcmp(scenario, "breakpoint")) {
      std::fflush(stdout);
      __debugbreak();
      return 0;
   }
   if (!std::strcmp(scenario, "breakpoint_with_diagnostic")) {
      printDiagnostic();
      __debugbreak();
      return 0;
   }
   if (!std::strcmp(scenario, "seh_custom")) {
      std::fflush(stdout);
      RaiseException(0xE0000001u, 0, 0, nullptr);
      return 0;
   }
#endif

   std::fprintf(stderr, "unknown scenario '%s'\n", scenario);
   return 2;
}
