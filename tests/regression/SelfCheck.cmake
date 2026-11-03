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
