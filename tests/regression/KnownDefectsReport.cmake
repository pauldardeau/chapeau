# Runs every known-defect reproduction once and prints what happens to each.
# This is a report, not a test: it exits 0 whatever the outcomes are, because
# the point is to list them. (The CTest cases, enabled with
# -DCHAPEAU_KNOWN_DEFECTS=ON, are the ones that fail.)
#
#   cmake -DEXE=<chapeau_probe> -DWORK=<dir>/case-tmp/<name>
#         -DDEFECTS=<known_defects.cmake> -P KnownDefectsReport.cmake
#
# Outcomes are classified as in Classify.cmake. "success" in the last column
# means the reproduction found nothing: either the defect was fixed or, for
# the use-after-free cases, the build has no sanitizer to see it with.

cmake_minimum_required(VERSION 3.16)

get_filename_component(here "${CMAKE_CURRENT_LIST_FILE}" DIRECTORY)
include("${here}/Classify.cmake")
include("${DEFECTS}")

if(NOT IS_ABSOLUTE "${WORK}" OR NOT "${WORK}" MATCHES "/case-tmp/[^/]+$")
   message(FATAL_ERROR "KnownDefectsReport.cmake: unsafe WORK '${WORK}'")
endif()

set(present 0)
set(seen 0)
message("")
message("id     scenario                                          outcome")
message("-----  ------------------------------------------------  -------------------------")

foreach(entry IN LISTS CHAPEAU_DEFECT_TABLE)
   string(REPLACE "|" ";" fields "${entry}")
   list(GET fields 0 id)
   list(GET fields 1 scenario)
   list(GET fields 2 timeout)

   string(TIMESTAMP stamp "%Y%m%d%H%M%S")
   string(RANDOM LENGTH 8 ALPHABET "0123456789abcdef" suffix)
   set(tempDir "${WORK}/run-${stamp}-${suffix}")
   file(MAKE_DIRECTORY "${tempDir}")
   set(ENV{TMPDIR} "${tempDir}")
   set(ENV{TMP} "${tempDir}")
   set(ENV{TEMP} "${tempDir}")
   set(ENV{TEMPDIR} "${tempDir}")

   execute_process(
      COMMAND "${EXE}" ${scenario}
      RESULT_VARIABLE rc
      OUTPUT_VARIABLE out
      ERROR_VARIABLE err
      TIMEOUT ${timeout}
   )
   file(REMOVE_RECURSE "${tempDir}")

   chapeau_classify("${rc}" "${out}${err}" outcome)
   if(NOT "${outcome}" STREQUAL "success")
      math(EXPR present "${present} + 1")
   endif()
   math(EXPR seen "${seen} + 1")

   string(SUBSTRING "${id}      " 0 5 idCol)
   string(SUBSTRING "${scenario}                                                  " 0 48 scenarioCol)
   message("${idCol}  ${scenarioCol}  ${outcome}")
endforeach()

file(GLOB remaining "${WORK}/*")
if(NOT remaining)
   file(REMOVE_RECURSE "${WORK}")
endif()

message("")
message("${present} of ${seen} reproductions still show a problem; "
        "'success' means nothing was observed (fixed, or no sanitizer to see it).")
