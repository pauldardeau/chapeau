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

A case states what it expects: `success` (the default) or `exit:<n>`. It
passes only if the outcome equals that. There is no way to expect a crash, a
hang or a sanitizer report, so none of them can pass as "expected". A hung child
is killed after `TIMEOUT` seconds (default 60) and reported as `timeout`; it
cannot block the run. Each execution gets its own temporary directory (via
`TMPDIR`/`TMP`/`TEMP`), which is removed afterwards; a child that ended normally
and left files behind fails the case.

`harness_selfcheck` (`SelfCheck.cmake` + `harness_helper.cpp`) proves all of
this against a helper that deliberately succeeds, exits 3, aborts, dies of
SIGSEGV, hangs, prints sanitizer-style reports, and leaves or cleans up files.

## Three sets of scenarios

* **Default test set** (always registered): `harness_selfcheck` and the
  `regression_control_*` cases — behaviour that works today, run in isolated
  processes. Labels `harness` and `regression`.
* **Known defects** (`ch<nn>_*` in `chapeau_probe.cpp`, listed in
  `CHAPEAU_DEFECT_TABLE` in `known_defects.cmake`): reproductions of the H1a
  defects that are not fixed yet (CH-06, and the busy-wait cases of CH-07). Scenarios whose defect has been
  fixed (CH-01 … CH-05, and the two CH-07 cases that were the same lifetime
  problem) are in `CHAPEAU_FIXED_TABLE` and run in the default set as
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
* **Characterization** (`characterize_*`): prints today's behaviour without
  asserting anything. Used for CH-08, which needs an error-reporting API before
  a test can say what is correct.

## Seeing the defects

The use-after-free defects (CH-01, CH-02) read freed memory; in a plain build
they may crash, return garbage, or appear to work. Run the known-defect set in an
AddressSanitizer build (`-DCMAKE_CXX_FLAGS=-fsanitize=address
-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address`) for a reliable verdict. A scenario
that ends in `success` in a plain build is not evidence that its defect is fixed.

The harness is POSIX-only (signals); it is skipped on Windows.
