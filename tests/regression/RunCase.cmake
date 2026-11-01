# Runs one scenario of a helper executable in its own process and checks how
# it ended. Invoked by CTest as
#
#   cmake -DEXE=<path> -DSCENARIO=<name> -DEXPECT=success|exit:<n>
#         [-DTIMEOUT=<seconds>] [-DKNOWN_DEFECT=<id>]
#         [-DMATCH_1=<regex> ...] [-DREJECT_1=<regex> ...]
#         [-DTEMP_ROOT=<dir>] -P RunCase.cmake
#
# The outcome is classified (see Classify.cmake) as success, exit:<n>, crash,
# timeout or sanitizer, and the case passes only if it equals EXPECT:
#   - a crash, a hang or a sanitizer report never equals EXPECT, so it can
#     never be an "expected" result (there is deliberately no way to say
#     "expect a crash");
#   - a different exit status than EXPECT fails, including exit:1 vs success;
#   - the child gets TIMEOUT seconds (default 60) and is killed after that, so
#     a hung child can neither block the suite nor be mistaken for a failure
#     of some other kind: it is reported as "timeout".
#
# KNOWN_DEFECT only changes the wording of the failure ("known defect CH-nn is
# still present"); the case still fails, and tests of known defects are not
# registered in the default test set at all (see CMakeLists.txt).
#
# TEMP_ROOT gives the child a temporary directory of its own, unique to this
# execution, via TMPDIR/TMP/TEMP/TEMPDIR. Only that directory is removed. If
# the child ended normally (success or exit:<n>) and left anything in it, the
# case fails and lists what was left; after a crash or a kill leftovers are
# expected and just removed.

cmake_minimum_required(VERSION 3.16)

get_filename_component(here "${CMAKE_CURRENT_LIST_FILE}" DIRECTORY)
include("${here}/Classify.cmake")

if(NOT DEFINED EXE OR NOT DEFINED SCENARIO OR NOT DEFINED EXPECT)
   message(FATAL_ERROR "RunCase.cmake: EXE, SCENARIO and EXPECT are required")
endif()
if(NOT "${EXPECT}" MATCHES "^(success|exit:[0-9]+)$")
   message(FATAL_ERROR "RunCase.cmake: EXPECT must be success or exit:<n>, not '${EXPECT}'")
endif()
if(NOT DEFINED TIMEOUT)
   set(TIMEOUT 60)
endif()

# ---- per-execution temporary workspace --------------------------------------

set(tempDir "")
if(DEFINED TEMP_ROOT)
   # the only directories this script creates or deletes are below a
   # "case-tmp/<name>" directory that the build tree made for this test
   if(NOT IS_ABSOLUTE "${TEMP_ROOT}" OR NOT "${TEMP_ROOT}" MATCHES "/case-tmp/[^/]+$")
      message(FATAL_ERROR "RunCase.cmake: unsafe TEMP_ROOT '${TEMP_ROOT}'")
   endif()
   string(TIMESTAMP stamp "%Y%m%d%H%M%S")
   string(RANDOM LENGTH 10 ALPHABET "0123456789abcdef" suffix)
   set(tempDir "${TEMP_ROOT}/run-${stamp}-${suffix}")
   if(EXISTS "${tempDir}")
      message(FATAL_ERROR "RunCase.cmake: '${tempDir}' already exists")
   endif()
   file(MAKE_DIRECTORY "${tempDir}")
   # inherited by the child started below
   set(ENV{TMPDIR} "${tempDir}")
   set(ENV{TMP} "${tempDir}")
   set(ENV{TEMP} "${tempDir}")
   set(ENV{TEMPDIR} "${tempDir}")
endif()

# readable sanitizer reports unless the caller chose its own options
if(NOT DEFINED ENV{UBSAN_OPTIONS})
   set(ENV{UBSAN_OPTIONS} "print_stacktrace=1")
endif()

string(TIMESTAMP started "%s")
execute_process(
   COMMAND "${EXE}" ${SCENARIO}
   RESULT_VARIABLE rc
   OUTPUT_VARIABLE out
   ERROR_VARIABLE err
   TIMEOUT ${TIMEOUT}
)
string(TIMESTAMP finished "%s")
math(EXPR elapsed "${finished} - ${started}")

set(all "${out}${err}")
string(REPLACE "\r" "" all "${all}")

chapeau_classify("${rc}" "${all}" outcome)

# ---- cleanup (before any check, so a failing check cannot skip it) ----------

set(leftoverProblem "")
if(tempDir)
   file(GLOB_RECURSE leftovers LIST_DIRECTORIES true "${tempDir}/*")
   file(REMOVE_RECURSE "${tempDir}")
   if(EXISTS "${tempDir}")
      set(leftoverProblem "could not remove the test directory '${tempDir}'")
   elseif(leftovers AND ("${outcome}" STREQUAL "success" OR "${outcome}" MATCHES "^exit:"))
      set(leftoverProblem "the test left files behind: ${leftovers}")
   endif()
   # this test's own (now empty) workspace as well
   file(GLOB remaining "${TEMP_ROOT}/*")
   if(NOT remaining)
      file(REMOVE_RECURSE "${TEMP_ROOT}")
   endif()
endif()

message("scenario: ${SCENARIO}")
message("expected: ${EXPECT}")
message("outcome: ${outcome} (${elapsed}s)")

function(case_fail message)
   message("---- output of ${EXE} ${SCENARIO} ----\n${all}\n---- end of output ----")
   if(leftoverProblem)
      message("---- cleanup problem ----\n${leftoverProblem}")
   endif()
   message(FATAL_ERROR "${message}")
endfunction()

if(NOT "${outcome}" STREQUAL "${EXPECT}")
   if(DEFINED KNOWN_DEFECT AND NOT "${KNOWN_DEFECT}" STREQUAL "")
      case_fail("known defect ${KNOWN_DEFECT} is still present: ${SCENARIO} ended with '${outcome}', a fixed implementation ends with '${EXPECT}'")
   endif()
   case_fail("unexpected outcome '${outcome}' for ${SCENARIO} (expected '${EXPECT}')")
endif()

foreach(i RANGE 1 20)
   if(DEFINED MATCH_${i})
      if(NOT all MATCHES "${MATCH_${i}}")
         case_fail("output does not match expected pattern: ${MATCH_${i}}")
      endif()
   endif()
   if(DEFINED REJECT_${i})
      if(all MATCHES "${REJECT_${i}}")
         case_fail("output unexpectedly matches pattern: ${REJECT_${i}}")
      endif()
   endif()
endforeach()

if(leftoverProblem)
   case_fail("temporary workspace problem: ${leftoverProblem}")
endif()

message("OK: ${SCENARIO} ended with '${outcome}' as expected")
