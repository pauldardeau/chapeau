# Checks the regression harness itself: that RunCase.cmake tells the ways a
# child can end apart, and refuses every one of them that is not what the case
# expects. It runs RunCase.cmake as a child process against harness_helper and
# inspects the driver's exit status and message.
#
#   cmake -DCMAKE=<cmake> -DDRIVER=<RunCase.cmake> -DHELPER=<harness_helper>
#         -DWORK=<scratch dir under the build tree> -P SelfCheck.cmake
#
# Included here, deliberately: the cases where the driver must FAIL (a crash
# or hang where success is expected, and so on). Those prove that no crash,
# hang or sanitizer report can ever be counted as an expected pass.

cmake_minimum_required(VERSION 3.16)

foreach(var CMAKE DRIVER HELPER WORK)
   if(NOT DEFINED ${var})
      message(FATAL_ERROR "SelfCheck.cmake: ${var} is required")
   endif()
endforeach()

if(NOT IS_ABSOLUTE "${WORK}" OR NOT "${WORK}" MATCHES "/selfcheck-work$")
   message(FATAL_ERROR "SelfCheck.cmake: unsafe WORK '${WORK}'")
endif()

set(problems "")

# the classification rules themselves (Classify.cmake is the driver's own file)
get_filename_component(driverDir "${DRIVER}" DIRECTORY)
include("${driverDir}/Classify.cmake")

# expect_classified(<label> <execute_process result text> <the outcome it must be classified as>)
function(expect_classified label rc expected)
   chapeau_classify("${rc}" "" got)
   if("${got}" STREQUAL "${expected}")
      message("self-check ok: ${label}")
   else()
      set(problems "${problems}\n[${label}] '${rc}' was classified '${got}', expected '${expected}'" PARENT_SCOPE)
   endif()
endfunction()

# expect_failfast(<label> <outcome> <TRUE|FALSE>)
function(expect_failfast label outcome expected)
   chapeau_is_failfast("${outcome}" got)
   if("${got}" STREQUAL "${expected}")
      message("self-check ok: ${label}")
   else()
      set(problems "${problems}\n[${label}] '${outcome}' is a fast fail: ${got}, expected ${expected}" PARENT_SCOPE)
   endif()
endfunction()

# run_driver(<label> <expect: pass|fail> <outcome the driver must report>
#            <extra regex the message must match, or ""> <driver args...>)
function(run_driver label expect reportedOutcome messageRegex)
   set(driverArgs "")
   foreach(a IN LISTS ARGN)
      list(APPEND driverArgs "${a}")
   endforeach()

   string(TIMESTAMP t0 "%s")
   execute_process(
      COMMAND "${CMAKE}" "-DEXE=${HELPER}" ${driverArgs} -P "${DRIVER}"
      RESULT_VARIABLE rc
      OUTPUT_VARIABLE out
      ERROR_VARIABLE err
      TIMEOUT 120
   )
   string(TIMESTAMP t1 "%s")
   math(EXPR seconds "${t1} - ${t0}")
   set(text "${out}${err}")

   set(failed "")
   if("${expect}" STREQUAL "pass")
      if(NOT "${rc}" STREQUAL "0")
         string(APPEND failed " driver failed (rc=${rc}) but should have passed;")
      endif()
   else()
      if("${rc}" STREQUAL "0")
         string(APPEND failed " driver passed but should have failed;")
      endif()
   endif()
   if(NOT "${reportedOutcome}" STREQUAL "")
      if(NOT text MATCHES "outcome: ${reportedOutcome}")
         string(APPEND failed " driver did not report outcome '${reportedOutcome}';")
      endif()
   endif()
   if(NOT "${messageRegex}" STREQUAL "" AND NOT text MATCHES "${messageRegex}")
      string(APPEND failed " message does not match '${messageRegex}';")
   endif()

   if(failed)
      set(problems "${problems}\n[${label}]${failed}\n---- driver output ----\n${text}\n---- end ----" PARENT_SCOPE)
   else()
      message("self-check ok: ${label} (${seconds}s)")
   endif()
   set(lastSeconds ${seconds} PARENT_SCOPE)
endfunction()

# ---- how a child can end, and what the driver must say ----------------------

run_driver("success is accepted when success is expected" pass success ""
   -DSCENARIO=ok -DEXPECT=success)

run_driver("an exact nonzero exit is accepted when it is expected" pass "exit:3" ""
   -DSCENARIO=exit3 -DEXPECT=exit:3)

run_driver("a nonzero exit is refused when success is expected" fail "exit:3"
   "unexpected outcome"
   -DSCENARIO=exit3 -DEXPECT=success)

run_driver("a different nonzero exit is refused" fail "exit:3" ""
   -DSCENARIO=exit3 -DEXPECT=exit:4)

run_driver("success is refused when a nonzero exit is expected" fail success ""
   -DSCENARIO=ok -DEXPECT=exit:3)

run_driver("abort is a crash, refused when success is expected" fail "crash:" ""
   -DSCENARIO=abort -DEXPECT=success)

run_driver("a crash can never satisfy an expected exit status" fail "crash:" ""
   -DSCENARIO=segv -DEXPECT=exit:1)

run_driver("SIGSEGV is a crash, refused when success is expected" fail "crash:" ""
   -DSCENARIO=segv -DEXPECT=success)

run_driver("a sanitizer report is refused even with exit status 0" fail sanitizer ""
   -DSCENARIO=sanitizer_exit0 -DEXPECT=success)

run_driver("a sanitizer report is not an ordinary exit:1" fail sanitizer ""
   -DSCENARIO=sanitizer_exit1 -DEXPECT=exit:1)

run_driver("a sanitizer start-up failure is not an ordinary exit either" fail sanitizer ""
   -DSCENARIO=sanitizer_fatal -DEXPECT=exit:66)

# a hung child is killed after TIMEOUT and is reported as a timeout, not as a
# generic failure, and it does not hold the run up
run_driver("a hung child is killed and reported as a timeout" fail timeout ""
   -DSCENARIO=hang -DEXPECT=success -DTIMEOUT=2)
if(lastSeconds GREATER 30)
   set(problems "${problems}\n[hang] the driver took ${lastSeconds}s for a 2s timeout")
endif()

# EXPECT=abort: the one narrow exception to "a crash can never be expected". It needs
# SIGABRT, and the diagnostic, and nothing else.
run_driver("an abort that says why is accepted when an abort is expected" pass "crash:"
   "OK: abort_with_diagnostic ended with"
   -DSCENARIO=abort_with_diagnostic -DEXPECT=abort "-DABORT_DIAGNOSTIC=helper fatal: reason X")

run_driver("an abort that does not say why is refused" fail "crash:"
   "does not hold the required diagnostic"
   -DSCENARIO=abort -DEXPECT=abort "-DABORT_DIAGNOSTIC=helper fatal: reason X")

run_driver("a different crash is refused even if it said the right thing" fail "crash:"
   "expected an abort"
   -DSCENARIO=segv_with_diagnostic -DEXPECT=abort "-DABORT_DIAGNOSTIC=helper fatal: reason X")

run_driver("success is refused when an abort is expected" fail success ""
   -DSCENARIO=ok -DEXPECT=abort "-DABORT_DIAGNOSTIC=hello")

run_driver("an exit status is refused when an abort is expected" fail "exit:3" ""
   -DSCENARIO=exit3 -DEXPECT=abort "-DABORT_DIAGNOSTIC=exits")

run_driver("a hang is refused when an abort is expected" fail timeout ""
   -DSCENARIO=hang -DEXPECT=abort "-DABORT_DIAGNOSTIC=hangs" -DTIMEOUT=2)

run_driver("a sanitizer report outranks an abort that says the right thing" fail sanitizer ""
   -DSCENARIO=sanitizer_then_abort -DEXPECT=abort "-DABORT_DIAGNOSTIC=helper fatal: reason X")

run_driver("an expected abort needs a diagnostic to look for" fail ""
   "requires ABORT_DIAGNOSTIC"
   -DSCENARIO=abort_with_diagnostic -DEXPECT=abort)

run_driver("an abort that is NOT expected still fails with its diagnostic present" fail "crash:" ""
   -DSCENARIO=abort_with_diagnostic -DEXPECT=success)

# ---- Windows only: abort() is a fast fail, and some crashes are plain numbers -------------
# (POSIX has none of this; its checks above are the whole of its self-check.)
if(CMAKE_HOST_WIN32)
   # the classification rules, without a process
   math(EXPR failfastSigned "0xC0000409 - 4294967296")
   if(NOT "${failfastSigned}" STREQUAL "-1073740791")
      set(problems "${problems}\n[constant] 0xC0000409 as a signed 32-bit number is ${failfastSigned}, not -1073740791")
   endif()
   expect_classified("0 is success" "0" "success")
   expect_classified("3 is an ordinary exit status" "3" "exit:3")
   expect_classified("255 is an ordinary exit status" "255" "exit:255")
   expect_classified("a number above 255 is a crash" "256" "crash:status 256")
   expect_classified("a negative number is a crash" "-1" "crash:status -1")
   expect_classified("the unhandled breakpoint (0x80000003), reported as a number, is a crash" "-2147483645" "crash:status -2147483645")
   expect_classified("a custom SEH exception (0xE0000001), reported as a number, is a crash" "-536870911" "crash:status -536870911")
   expect_classified("0xC0000409 reported as a signed number is a crash" "-1073740791" "crash:status -1073740791")
   expect_classified("a textual crash result stays a crash" "Access violation" "crash:Access violation")
   expect_classified("the fast-fail text stays a crash" "Exit code 0xc0000409" "crash:Exit code 0xc0000409")
   expect_classified("a timeout stays a timeout" "Process terminated due to timeout" "timeout")
   expect_failfast("the fast-fail text is a fast fail" "crash:Exit code 0xc0000409\n" TRUE)
   expect_failfast("the same in upper case is a fast fail" "crash:Exit code 0xC0000409" TRUE)
   expect_failfast("0xC0000409 as a signed number is a fast fail" "crash:status -1073740791" TRUE)
   expect_failfast("an access violation is not a fast fail" "crash:Access violation" FALSE)
   expect_failfast("another Exit code text is not a fast fail" "crash:Exit code 0xc0000005" FALSE)
   expect_failfast("an unhandled breakpoint is not a fast fail" "crash:status -2147483645" FALSE)
   expect_failfast("an ordinary exit 3 is not a fast fail" "exit:3" FALSE)
   expect_failfast("success is not a fast fail" "success" FALSE)
   expect_failfast("a timeout is not a fast fail" "timeout" FALSE)

   # whole processes
   run_driver("exit status 255 is accepted when it is expected" pass "exit:255" ""
      -DSCENARIO=exit255 -DEXPECT=exit:255)
   run_driver("exit status 255 is not exit status 3" fail "exit:255" ""
      -DSCENARIO=exit255 -DEXPECT=exit:3)
   run_driver("an unhandled breakpoint is a crash, refused when success is expected" fail "crash:" ""
      -DSCENARIO=breakpoint -DEXPECT=success)
   run_driver("an unhandled breakpoint can never satisfy an expected exit status" fail "crash:" ""
      -DSCENARIO=breakpoint -DEXPECT=exit:3)
   run_driver("a custom SEH exception is a crash, refused when success is expected" fail "crash:" ""
      -DSCENARIO=seh_custom -DEXPECT=success)
   # (CMake wraps long messages at about 80 columns, so these patterns accept a line break wherever the text has a space)
   run_driver("a diagnostic followed by an ordinary exit 3 is not an abort" fail "exit:3"
      "unexpected[ \t\n]+outcome"
      -DSCENARIO=exit3_with_diagnostic -DEXPECT=abort "-DABORT_DIAGNOSTIC=helper fatal: reason X")
   run_driver("a diagnostic followed by a plain return of 0xC0000409 is not an abort" fail "crash:"
      "did[ \t\n]+not[ \t\n]+report[ \t\n]+SIGABRT"
      -DSCENARIO=return_failfast_status_with_diagnostic -DEXPECT=abort "-DABORT_DIAGNOSTIC=helper fatal: reason X")
   run_driver("a fast fail that did not go through abort() has no SIGABRT marker and is not an abort" fail "crash:"
      "did[ \t\n]+not[ \t\n]+report[ \t\n]+SIGABRT"
      -DSCENARIO=failfast_with_diagnostic -DEXPECT=abort "-DABORT_DIAGNOSTIC=helper fatal: reason X")
   run_driver("a diagnostic followed by an unhandled breakpoint is not an abort" fail "crash:"
      "expected[ \t\n]+an[ \t\n]+abort"
      -DSCENARIO=breakpoint_with_diagnostic -DEXPECT=abort "-DABORT_DIAGNOSTIC=helper fatal: reason X")
   run_driver("a diagnostic followed by a hang is not an abort" fail timeout ""
      -DSCENARIO=hang_with_diagnostic -DEXPECT=abort "-DABORT_DIAGNOSTIC=helper fatal: reason X" -DTIMEOUT=2)
endif()

# KNOWN_DEFECT changes only the wording
run_driver("a known defect that still crashes keeps failing" fail "crash:"
   "known defect CH-00 is still present"
   -DSCENARIO=abort -DEXPECT=success -DKNOWN_DEFECT=CH-00)

# output patterns
run_driver("a required output pattern that is present" pass success ""
   -DSCENARIO=ok -DEXPECT=success "-DMATCH_1=helper says hello")
run_driver("a required output pattern that is missing" fail success
   "does not match expected pattern"
   -DSCENARIO=ok -DEXPECT=success "-DMATCH_1=not printed")
run_driver("a rejected output pattern that is present" fail success
   "unexpectedly matches"
   -DSCENARIO=ok -DEXPECT=success "-DREJECT_1=hello")

# temporary workspace
set(tmpRoot "${WORK}/case-tmp/selfcheck")
file(REMOVE_RECURSE "${WORK}")
run_driver("a child that cleans up after itself" pass success ""
   -DSCENARIO=clean_temp_file -DEXPECT=success "-DTEMP_ROOT=${tmpRoot}")
run_driver("a child that leaves a file behind is refused" fail success
   "left files behind"
   -DSCENARIO=leave_temp_file -DEXPECT=success "-DTEMP_ROOT=${tmpRoot}")
run_driver("an unsafe TEMP_ROOT is refused before anything is removed" fail ""
   "unsafe TEMP_ROOT"
   -DSCENARIO=ok -DEXPECT=success "-DTEMP_ROOT=${WORK}/elsewhere")

if(EXISTS "${tmpRoot}")
   file(GLOB leftoverRuns "${tmpRoot}/*")
   if(leftoverRuns)
      set(problems "${problems}\n[cleanup] the driver left ${leftoverRuns} behind")
   endif()
endif()
file(REMOVE_RECURSE "${WORK}")

if(problems)
   message(FATAL_ERROR "harness self-check failed:${problems}")
endif()
message("harness self-check: all cases behaved as required")
