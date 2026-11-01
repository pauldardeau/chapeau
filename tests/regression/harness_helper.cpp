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
//                      plain crash)
// hang                 never ends
// sanitizer_exit0      prints a UBSan-style report and exits 0
// sanitizer_exit1      prints an ASan-style report and exits 1
// sanitizer_fatal      prints a sanitizer start-up failure (not a finding) and exits 66
// leave_temp_file      leaves a file in its temporary directory, exits 0
// clean_temp_file      uses and removes a file in its temporary directory, exits 0
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <thread>
#include <chrono>

namespace fs = std::filesystem;

int main(int argc, char** argv) {
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
   if (!std::strcmp(scenario, "segv")) {
      std::fflush(stdout);
      std::signal(SIGSEGV, SIG_DFL);
      std::raise(SIGSEGV);
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

   std::fprintf(stderr, "unknown scenario '%s'\n", scenario);
   return 2;
}
