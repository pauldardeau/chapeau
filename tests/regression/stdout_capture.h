#ifndef CHAPEAU_REGRESSION_STDOUT_CAPTURE_H
#define CHAPEAU_REGRESSION_STDOUT_CAPTURE_H

// Private to the subprocess regression harness (tests/regression); not installed, not used by Chapeau itself.
//
//   std::string out = chapeau_harness::captureStdout([&] { ... });
//
// runs the body with C-level stdout (file descriptor 1) redirected into a private file and returns everything written to it, whoever
// wrote it: ::printf in the library under test, std::puts, write(1, ...). The scenario's own CHECK lines printed inside the body are
// captured too, as they always were. The same code runs on POSIX (dup, dup2, fileno, close) and on Windows/MSVC (the UCRT's _dup,
// _dup2, _fileno, _close).
//
// What it guarantees:
//  * The capture file lives in a ScopedTempDir of its own (a new, unique directory under the temporary directory for every capture;
//    no fixed path, nothing shared between processes or between captures), which is removed again before captureStdout() returns.
//  * stdout is put back when the body returns AND when it throws (the destructor does it). The body's exception then propagates
//    unchanged; a failure to restore in that situation is reported on stderr and never replaces the exception.
//  * A capture that cannot be set up, flushed, restored, read or cleaned up throws CaptureError. It never returns an empty string
//    as a stand-in for a capture that did not happen: an empty result always means the body really wrote nothing.
//
// Not supported: capturing from two threads at once, or while other threads write to stdout. Descriptor 1 is process-wide. The
// probes capture on their main thread only. Captures may be nested (restoration is last-in first-out).
//
// Not a general utility: it is meant to be reused by the other probes of this directory (the opt-in threadsafe0 probe has its own
// POSIX-only copy that W1c-3 replaces by this one).

#include <cstdio>
#include <cerrno>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <system_error>

#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

#include "scoped_temp_dir.h"

namespace chapeau_harness {

class CaptureError : public std::runtime_error {
public:
   explicit CaptureError(const std::string& what) : std::runtime_error("stdout capture: " + what) {}
};

namespace capture_detail {

inline int duplicateDescriptor(int fd) {
#ifdef _WIN32
   return _dup(fd);
#else
   return ::dup(fd);
#endif
}

inline int duplicateOnto(int fd, int onto) {
#ifdef _WIN32
   return _dup2(fd, onto) == 0 ? 0 : -1;   // _dup2 returns 0 on success
#else
   return ::dup2(fd, onto) < 0 ? -1 : 0;   // dup2 returns the new descriptor
#endif
}

inline void closeDescriptor(int fd) {
#ifdef _WIN32
   _close(fd);
#else
   ::close(fd);
#endif
}

inline int descriptorOf(std::FILE* file) {
#ifdef _WIN32
   return _fileno(file);
#else
   return ::fileno(file);
#endif
}

inline std::FILE* openReadWrite(const std::string& path) {
#ifdef _WIN32
   std::FILE* file = nullptr;
   return fopen_s(&file, path.c_str(), "w+b") == 0 ? file : nullptr;
#else
   return std::fopen(path.c_str(), "w+b");
#endif
}

inline std::string describe(const std::string& what, int err) {
   return what + (err != 0 ? ": " + std::error_code(err, std::generic_category()).message() : std::string());
}

// The whole file from its beginning. false, with the reason, if it cannot be read: the result is then not to be used.
inline bool readAll(std::FILE* file, std::string& out, std::string& why) {
   out.clear();
   std::clearerr(file);
   errno = 0;
   if (std::fseek(file, 0, SEEK_SET) != 0) {
      why = describe("could not rewind the capture file", errno);
      return false;
   }
   char buffer[4096];
   for (;;) {
      errno = 0;
      const size_t got = std::fread(buffer, 1, sizeof buffer, file);
      out.append(buffer, got);
      if (got < sizeof buffer) {
         if (std::ferror(file) != 0) {
            why = describe("could not read the capture file", errno);
            return false;
         }
         return true;   // end of file
      }
   }
}

}

class StdoutCapture {
public:
   // Redirects stdout. Throws CaptureError, with nothing left redirected or created, if that is not possible.
   StdoutCapture() {
      using namespace capture_detail;
      std::fflush(stdout);
      try {
         m_dir.reset(new chapeau_tests::ScopedTempDir("chapeau-capture"));
      } catch (const std::exception& e) {
         throw CaptureError(std::string("no private directory for the capture file: ") + e.what());
      }
      errno = 0;
      m_file = openReadWrite(m_dir->file("stdout.txt"));
      if (m_file == nullptr) {
         throw CaptureError(describe("could not create the capture file", errno));
      }
      errno = 0;
      m_saved = duplicateDescriptor(1);
      if (m_saved < 0) {
         const int err = errno;
         std::fclose(m_file);
         m_file = nullptr;
         throw CaptureError(describe("could not duplicate stdout", err));
      }
      errno = 0;
      if (duplicateOnto(descriptorOf(m_file), 1) < 0) {
         const int err = errno;
         closeDescriptor(m_saved);
         std::fclose(m_file);
         m_file = nullptr;
         throw CaptureError(describe("could not redirect stdout", err));
      }
      m_redirected = true;
   }

   // If finish() was not reached (the body threw), stdout is put back here. Never throws.
   ~StdoutCapture() {
      using namespace capture_detail;
      if (m_redirected) {
         std::fflush(stdout);
         if (duplicateOnto(m_saved, 1) < 0) {
            std::fprintf(stderr, "stdout capture: could not restore stdout after the body ended with an exception\n");
         }
         closeDescriptor(m_saved);
         m_redirected = false;
      }
      if (m_file != nullptr) {
         std::fclose(m_file);   // before the directory (m_dir, destroyed afterwards) is removed
      }
   }

   // Puts stdout back and returns what was written. Throws CaptureError if any step failed; the result is never a substitute.
   std::string finish() {
      using namespace capture_detail;
      if (!m_redirected) {
         throw CaptureError("finish() without an active capture");
      }
      errno = 0;
      const bool flushed = std::fflush(stdout) == 0;
      const int flushError = errno;
      errno = 0;
      const bool restored = duplicateOnto(m_saved, 1) == 0;
      const int restoreError = errno;
      closeDescriptor(m_saved);
      m_redirected = false;
      if (!restored) {
         throw CaptureError(describe("could not restore stdout (it may still be redirected)", restoreError));
      }
      if (!flushed) {
         throw CaptureError(describe("could not flush the captured output", flushError));
      }
      std::string out;
      std::string why;
      if (!readAll(m_file, out, why)) {
         throw CaptureError(why);
      }
      std::fclose(m_file);
      m_file = nullptr;
      if (!m_dir->remove()) {
         throw CaptureError("could not remove the private capture directory");
      }
      return out;
   }

   StdoutCapture(const StdoutCapture&) = delete;
   StdoutCapture& operator=(const StdoutCapture&) = delete;

private:
   std::unique_ptr<chapeau_tests::ScopedTempDir> m_dir;   // declared first: destroyed last
   std::FILE* m_file = nullptr;
   int m_saved = -1;
   bool m_redirected = false;
};

inline std::string captureStdout(const std::function<void()>& body) {
   StdoutCapture capture;
   body();   // if it throws, ~StdoutCapture restores stdout and the exception goes on
   return capture.finish();
}

}

#endif
