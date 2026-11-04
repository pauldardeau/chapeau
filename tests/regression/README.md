# Subprocess regression harness

Some Chapeau defects crash the process, hang it, or read freed memory. Those
cannot be tested in-process: the test runner would die with them. This
directory runs each such scenario in its own process and checks how it ended.

## How a scenario ends

`RunCase.cmake` starts one scenario of a helper program with a time limit and
classifies the result (`Classify.cmake`), in this order:

| outcome       | meaning                                                              |
|---------------|----------------------------------------------------------------------|
| `timeout`     | the child had to be killed at its limit (a hang)                     |
| `sanitizer`   | the output holds an ASan/LSan/TSan report or a UBSan `runtime error:` — whatever the exit status |
| `crash:<why>` | killed by a signal or otherwise abnormal (not an exit status)        |
| `success`     | exit status 0                                                        |
| `exit:<n>`    | exit status `n`                                                      |

A case states what it expects: `success` (the default), `exit:<n>` or, in one
narrow case, `abort`. It passes only if the outcome equals that. There is no way
to expect a crash, a hang or a sanitizer report, so none of them can pass as
"expected".

`abort` exists because Chapeau's destructors end the process on purpose when
they meet a unit that is in use. A case with `EXPECT abort` must also give an
`ABORT_DIAGNOSTIC` (a regular expression), and passes only if the child was ended
by SIGABRT, the output holds the diagnostic, and the output holds no sanitizer
report (a report outranks the abort: the outcome is then `sanitizer`). Any other
crash, a different signal, an exit status, a hang, or an abort that did not say
why, fails the case. `harness_selfcheck` proves each of those refusals. A hung child
is killed after `TIMEOUT` seconds (default 60) and reported as `timeout`; it
cannot block the run. Each execution gets its own temporary directory (via
`TMPDIR`/`TMP`/`TEMP`), which is removed afterwards; a child that ended normally
and left files behind fails the case.

`harness_selfcheck` (`SelfCheck.cmake` + `harness_helper.cpp`) proves all of
this against a helper that deliberately succeeds, exits 3, aborts, dies of
SIGSEGV, hangs, prints sanitizer-style reports, and leaves or cleans up files.

## Sets of scenarios

* **Default test set** (always registered): `harness_selfcheck` and the
  `regression_control_*` cases — behaviour that works today, run in isolated
  processes. Labels `harness` and `regression`.
* **Known defects** (`ch<nn>_*` in `chapeau_probe.cpp`, listed in
  `CHAPEAU_DEFECT_TABLE` in `known_defects.cmake`): reproductions of the H1a
  defects that are not fixed yet. There are none left: every investigated critical
  defect (CH-01 … CH-08 and CH-11 … CH-13) is fixed, and its scenarios are in `CHAPEAU_FIXED_TABLE` and run in the default set as
  `regression_ch…` cases. Each unfixed scenario asserts what a *fixed* Chapeau
  does, so it fails today; these are **not** in the default set and not part of
  normal CI. Enable them with
  `-DCHAPEAU_KNOWN_DEFECTS=ON` (label `known-defect`):

  ```bash
  cmake -S . -B build-kd -DCHAPEAU_KNOWN_DEFECTS=ON
  cmake --build build-kd
  ctest --test-dir build-kd -L known-defect      # fails until the defects are fixed
  cmake --build build-kd --target chapeau_known_defects_report   # lists outcomes, never fails
  ```

  When a defect is fixed, move its line from `CHAPEAU_DEFECT_TABLE` to
  `CHAPEAU_FIXED_TABLE` in `known_defects.cmake`; it then runs in the default set.
* **Busy-timeout scenarios** (`busy_*` in `chapeau_probe.cpp`): how the busy
  timeout behaves and what a locked database does to queries, updates,
  transactions and `close()`. They wait for a lock on purpose, so they run in
  bounded subprocesses (default set, label `regression`).
* **Error-reporting scenarios** (`errors_*` in `chapeau_probe.cpp`, H1a-8): every
  kind of failure is fired with `setLogsErrors(true)` and `(false)`. The scenario
  captures the process's own stdout around each call, so it can say that exactly
  one `DB Error [...]` line was written (kind, code, message, operation, SQL) when
  logging is on, that nothing at all was written when it is off, and that the
  return value and the recorded error are the same either way. The busy failure
  of an update and of a cursor is checked in its own bounded process. (The
  capture is `stdout_capture.h`, the same code on POSIX and Windows; see below.)
* **Concurrency scenarios** (`concurrency_*` in `chapeau_concurrency_probe.cpp`,
  H1a-12a; labels `regression` and `concurrency`): the patterns that Chapeau
  supports (see the *Concurrency* section of the top-level README), and only
  those. Several threads each with a connection of their own on one file; a
  connection with a live cursor handed from one thread to another through a
  mutex and condition variable (the cursor carries on, the error snapshot and an
  open transaction move with the unit); the opening thread exiting, also through
  a chain of short-lived threads; cursors destroyed on another thread, and
  outliving their database across threads; and a record of `sqlite3_threadsafe()`.
  They never share a unit between two threads at once: that is not supported, so
  it is not tested as if it were, and no scenario is racy on purpose. They are
  meant to run clean under ThreadSanitizer (`-fsanitize=thread`; the CI job
  `tsan` runs them with `ctest -L concurrency`). Worker threads do not call
  `check()`; they record into a `Report` that the main thread prints after the
  join, and every wait has a limit that ends the process with a failed check.
  The waits use the system clock on purpose (see `waitFor()` in the probe): the
  ThreadSanitizer of GCC 11 mis-reports `condition_variable::wait_for()`.

* **Refusal scenarios** (`refusal_*` and `destructor_aborts_*` in
  `chapeau_concurrency_probe.cpp`, H1a-12g; labels `regression`, `concurrency`
  and `refusal`): a call made while another call of the same unit is running is
  refused, and a destructor that meets a unit in use ends the process. The owner
  is held inside the unit by a SQL function (`gate()`) that signals and waits
  for a release, so each case is deterministic: no sleeps, every wait bounded.
  The intruder threads call into the unit on purpose, and are refused; they must
  not race (they touch only atomics and their own thread-local record), so
  ThreadSanitizer stays silent. The cases: a refused `next()` against the end of
  the data; every cursor accessor refused (neutral values, out parameters cleared);
  a refusal while the owner records a SQLite error; two intruders at once
  (per-thread counts and records, the unit's sum); the log thresholds; `inUse()` as
  a silent probe; independent connections; a hand-off of a live cursor that is never
  refused; a refusal that leaves the owner's cached statement alone; a cursor that
  outlives its database; and four destructors that must abort (another thread, or
  the same thread from inside its own call; database and cursor), checked with
  `EXPECT abort`.

* **SQLite threading-mode programs** (`threadsafe0_probe.cpp`,
  `threadsafe2_probe.cpp`, H1a-12b; labels `regression` and `sqlite-threading`;
  **opt-in**, `-DCHAPEAU_THREADSAFE0_TEST=ON`): `open()` refuses a SQLite that was
  compiled without threading support (`sqlite3_threadsafe()` == 0) with
  `DBErrorKind::UnsupportedLibrary`, code 0. The only honest test of that is a
  SQLite that really is one, so CMake builds a pinned SQLite amalgamation (3.37.2,
  SHA-256 pinned in `threadsafe0.cmake`) with `-DSQLITE_THREADSAFE=0`, and again
  with `=2`, and compiles Chapeau's sources into each program so that they run on
  it instead of on the system SQLite. Nothing is mocked. The amalgamation is
  taken from `-DCHAPEAU_THREADSAFE0_SQLITE_DIR=<dir>` (no network) or, if that is
  empty, downloaded by `FetchContent` and checked against the pinned hash; an
  ordinary build never downloads anything. The `=0` program shows the error (kind,
  code, message with the SQLite version, logging once or not at all), and that
  nothing was opened: a counting VFS sees no `xOpen`, no file appears,
  `sqliteHandle()` is null and SQLite's memory use is unchanged (the counter is
  shown to work by a raw `sqlite3_open`). The `=2` (multi-thread) program shows
  that a library with some threading support is not refused. With the option on, the
  default 64 registered tests become 70: `regression_threadsafe0_library_is_what_the_build_says`,
  `_open_is_refused`, `_repeated_refusal_leaves_the_object_valid`,
  `_refusal_is_logged_once_and_silent_when_off`, and
  `regression_threadsafe2_library_is_what_the_build_says`, `_open_is_allowed`
  (`ctest -L sqlite-threading` runs just these six). The option makes
  `threadsafe0.cmake` enable the C language (the amalgamation is C); without the option
  no C compiler is needed. On Windows see "Windows" below.

## Seeing the defects

The use-after-free defects (CH-01, CH-02) read freed memory; in a plain build
they may crash, return garbage, or appear to work. Run the known-defect set in an
AddressSanitizer build (`-DCMAKE_CXX_FLAGS=-fsanitize=address
-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address`) for a reliable verdict. A scenario
that ends in `success` in a plain build is not evidence that its defect is fixed.

## Windows

The harness runs on native Windows with MSVC (Debug and Release) as well as on POSIX hosts; any
other host skips it. The test names, scenarios, labels, time limits and MATCH/REJECT
requirements are the same. What differs, and why:

* **Process preparation** (`process_setup.h`, called first in `main()` of
  `chapeau_probe`, `chapeau_concurrency_probe`, `harness_helper`, `threadsafe0_probe` and
  `threadsafe2_probe`; it does nothing on
  other platforms). It switches off the Windows Error Reporting / JIT-debugger dialogs
  (`SetErrorMode`), sends the debug CRT's assertion and error reports to stderr (a message
  box would hold the scenario until its time limit), makes `abort()` end in a fast fail
  (status `0xC0000409`) in Debug as well as in Release, and installs a `SIGABRT` handler
  that writes the line `CHAPEAU-HARNESS: SIGABRT` and returns, so `abort()` carries on as
  before. It installs no unhandled-exception filter.
* **What `abort` means** (`EXPECT abort`, the four destructor cases). The status
  `0xC0000409` is not unique to `abort()` (a failed stack check, `__fastfail`,
  `std::terminate()` and a program that simply returns it give it too), and `raise(SIGABRT)`
  or a Debug `abort()` without the preparation end with plain exit status 3. So a case passes
  only if the status is a fast fail, **and** the marker line is in the output, **and** the
  `ABORT_DIAGNOSTIC` is, **and** no sanitizer report is, as on POSIX. An exit status (3
  included), another crash, a plain return of `0xC0000409`, a hang or a missing diagnostic
  fails it.
* **Crashes shown as numbers.** `execute_process` names most Windows exceptions ("Access
  violation", "Exit code 0xc0000409", ...) but reports an unhandled breakpoint and the
  `0xE...` exceptions as plain numbers. On Windows a numeric result outside 0 to 255 is
  therefore classified as `crash:status <n>`; 0 to 255 keep their meaning. `harness_helper`'s
  `segv` is a real access violation on Windows (`raise(SIGSEGV)` only exits with status 3
  there). `SelfCheck.cmake` has Windows-only checks for all of this.
* **Stdout capture** (`stdout_capture.h`). The scenarios that say "exactly this line was
  written" or "nothing was printed" read the process's own stdout while the body runs.
  `captureStdout(body)` redirects file descriptor 1 into a file in a `ScopedTempDir` of its
  own (`dup`/`dup2` on POSIX, `_dup`/`_dup2` on Windows), puts it back afterwards (also when
  the body throws) and removes the directory. It captures what the library writes with
  `::printf` as well as anything else written to stdout, including the scenario's own CHECK
  lines inside the body. A capture that cannot be set up, read or restored throws
  `CaptureError`, which `main()` reports as a failed check: it is never an empty result,
  because an empty result means "nothing was printed". One capture at a time, from one
  thread (descriptor 1 is process-wide).
* **The opt-in SQLite-threading set** (`-DCHAPEAU_THREADSAFE0_TEST=ON`, 70 registered tests
  instead of 64) runs on Windows too. `threadsafe0_probe.cpp` uses the same
  `stdout_capture.h`; both opt-in probes call `prepareProcess()`. The pinned SQLite 3.37.2 amalgamation
  (`sqlite3.c` and `sqlite3.h`, checked against the SHA-256 values in `threadsafe0.cmake`) has to be
  supplied, unpacked, with `-DCHAPEAU_THREADSAFE0_SQLITE_DIR=<dir>`; without it CMake would
  download the pinned zip, as on POSIX. The library itself still needs a SQLite to link (the
  `SQLite3_INCLUDE_DIR` and `SQLite3_LIBRARY` of the default Windows configuration). Verified
  with the Visual Studio 17 2022 generator, x64, Debug and Release:

      cmake -S . -B build-ts0 -G "Visual Studio 17 2022" -A x64 ^
            -DSQLite3_INCLUDE_DIR=<dir> -DSQLite3_LIBRARY=<sqlite3 .lib> ^
            -DCHAPEAU_THREADSAFE0_TEST=ON -DCHAPEAU_THREADSAFE0_SQLITE_DIR=<amalgamation dir>
      cmake --build build-ts0 --config Release
      ctest --test-dir build-ts0 -C Release

  Expected: 70 of 70 passing (64 of 64 without the option). Not verified: other
  generators or compilers on Windows (Ninja, clang-cl, MinGW), and running these tests with
  chapeau configured as a subdirectory of another project (only the build of the
  `=2` program was tried there). `threadsafe2_probe.cpp` captures no output; it calls
  `prepareProcess()` like the other probes.
