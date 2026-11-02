# Subprocess regression harness.
#
# Each scenario runs in its own process under RunCase.cmake, which tells apart
#   success / an expected nonzero exit / a crash / a timeout / a sanitizer
#   report
# and never accepts the last three as an expected result. A hung scenario is
# killed at its time limit and cannot block the suite. See README.md.
#
# Default test set (labels "regression" and "harness"):
#   harness_selfcheck           proves the driver classifies and refuses correctly
#   regression_control_*        behaviour that works today, in isolated processes
#   regression_<fixed defects>  reproductions of defects once they are fixed
#
# NOT in the default test set: the known-defect reproductions. They assert
# correct behaviour that Chapeau does not have yet, so they fail (crash, hang
# or sanitizer report) until the defect is fixed. They are registered, under
# the label "known-defect", only with -DCHAPEAU_KNOWN_DEFECTS=ON:
#   cmake -DCHAPEAU_KNOWN_DEFECTS=ON ...; ctest -L known-defect
# or listed without failing the build:
#   cmake --build . --target chapeau_known_defects_report

if(NOT UNIX)
   message(STATUS "chapeau: subprocess regression harness skipped (POSIX only)")
   return()
endif()

option(CHAPEAU_KNOWN_DEFECTS
   "Register the known-defect reproductions as CTest cases (they FAIL until the defects are fixed)"
   OFF)

include(${CMAKE_CURRENT_SOURCE_DIR}/known_defects.cmake)

add_executable(chapeau_harness_helper harness_helper.cpp)

add_executable(chapeau_probe chapeau_probe.cpp)
target_link_libraries(chapeau_probe PRIVATE chapeau)
# scoped_temp_dir.h is shared with the in-process tests
target_include_directories(chapeau_probe PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)

foreach(target chapeau_harness_helper chapeau_probe)
   target_compile_features(${target} PRIVATE cxx_std_20)
   target_compile_options(${target} PRIVATE
      $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra>)
endforeach()

# chapeau_add_case(<test name> <target> <scenario>
#                  [EXPECT success|exit:<n>] [TIMEOUT <seconds>]
#                  [KNOWN_DEFECT <id>] [LABEL <label>]
#                  [MATCH <regex>...] [REJECT <regex>...])
# EXPECT is success unless given; there is no way to expect a crash or a hang.
# The CTest TIMEOUT is only a backstop 60 s beyond the driver's own limit.
function(chapeau_add_case name target scenario)
   cmake_parse_arguments(C "" "EXPECT;TIMEOUT;KNOWN_DEFECT;LABEL" "MATCH;REJECT" ${ARGN})
   if(NOT DEFINED C_EXPECT)
      set(C_EXPECT success)
   endif()
   if(NOT DEFINED C_TIMEOUT)
      set(C_TIMEOUT 60)
   endif()

   set(command ${CMAKE_COMMAND}
      "-DEXE=$<TARGET_FILE:${target}>"
      "-DSCENARIO=${scenario}"
      "-DEXPECT=${C_EXPECT}"
      "-DTIMEOUT=${C_TIMEOUT}"
      "-DTEMP_ROOT=${CMAKE_CURRENT_BINARY_DIR}/case-tmp/${name}")

   if(C_KNOWN_DEFECT)
      list(APPEND command "-DKNOWN_DEFECT=${C_KNOWN_DEFECT}")
   endif()

   set(i 0)
   foreach(pattern IN LISTS C_MATCH)
      math(EXPR i "${i} + 1")
      list(APPEND command "-DMATCH_${i}=${pattern}")
   endforeach()

   set(i 0)
   foreach(pattern IN LISTS C_REJECT)
      math(EXPR i "${i} + 1")
      list(APPEND command "-DREJECT_${i}=${pattern}")
   endforeach()

   list(APPEND command -P "${CMAKE_CURRENT_SOURCE_DIR}/RunCase.cmake")

   add_test(NAME ${name} COMMAND ${command})
   math(EXPR backstop "${C_TIMEOUT} + 60")
   set_tests_properties(${name} PROPERTIES TIMEOUT ${backstop} LABELS "${C_LABEL}")
endfunction()

# ---- the harness itself ------------------------------------------------------

add_test(
   NAME harness_selfcheck
   COMMAND ${CMAKE_COMMAND}
      "-DCMAKE=${CMAKE_COMMAND}"
      "-DDRIVER=${CMAKE_CURRENT_SOURCE_DIR}/RunCase.cmake"
      "-DHELPER=$<TARGET_FILE:chapeau_harness_helper>"
      "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/selfcheck-work"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/SelfCheck.cmake"
)
set_tests_properties(harness_selfcheck PROPERTIES TIMEOUT 300 LABELS "harness")

# ---- controls: behaviour that works today ------------------------------------

foreach(scenario
   control_interleaved_cursors_different_sql
   control_reuse_after_partial_iteration
   control_zero_rows_then_rows
   control_reuse_after_constraint_error
   control_changing_params_same_sql
   control_busy_limit_gives_up)
   chapeau_add_case(regression_${scenario} chapeau_probe ${scenario}
      TIMEOUT 30 LABEL regression)
endforeach()

# the busy timeout, and what a locked database does to each kind of call: the
# scenarios wait for a lock on purpose, so they run in bounded subprocesses
foreach(scenario
   busy_zero_timeout_fails_at_once
   busy_positive_timeout_waits_then_fails
   busy_timeout_before_and_after_open
   busy_update_fails_then_recovers
   busy_close_under_contention)
   chapeau_add_case(regression_${scenario} chapeau_probe ${scenario}
      TIMEOUT 60 LABEL regression)
endforeach()

# how errors are reported (H1a-8): logged once, with kind, code and message, only
# when setLogsErrors() is on; the busy details in one bounded process
foreach(scenario
   errors_logged_once_with_kind_code_and_message
   errors_silent_when_logging_is_off
   errors_busy_is_recorded_and_recovers)
   chapeau_add_case(regression_${scenario} chapeau_probe ${scenario}
      TIMEOUT 60 LABEL regression)
endforeach()

# the messages printed for a failed sqlite3_step: SQLite's own code and text,
# and no "Unknown error" for a code SQLite defines
chapeau_add_case(regression_diagnostics_step_error_messages chapeau_probe
   diagnostics_step_error_messages
   TIMEOUT 60 LABEL regression
   MATCH
      "DB Error [[]Sqlite[]] code=19: UNIQUE constraint failed: t[.]id in executeUpdate"
      "DB Error [[]Sqlite[]] code=19: UNIQUE constraint failed: t[.]id in next[(][)]"
      "DB Error [[]Sqlite[]] code=1: integer overflow in next[(][)]"
      "DB Error [[]Sqlite[]] code=5: database is locked [(]gave up after waiting 100 ms[)] in executeUpdate"
   REJECT
      "Unknown error")

# ---- reproductions of defects that have been fixed ---------------------------

foreach(entry IN LISTS CHAPEAU_FIXED_TABLE)
   string(REPLACE "|" ";" fields "${entry}")
   list(GET fields 0 id)
   list(GET fields 1 scenario)
   list(GET fields 2 timeout)
   chapeau_add_case(regression_${scenario} chapeau_probe ${scenario}
      TIMEOUT ${timeout} LABEL regression)
endforeach()

# ---- known defects: opt-in, failing until fixed ------------------------------

if(CHAPEAU_KNOWN_DEFECTS)
   foreach(entry IN LISTS CHAPEAU_DEFECT_TABLE)
      string(REPLACE "|" ";" fields "${entry}")
      list(GET fields 0 id)
      list(GET fields 1 scenario)
      list(GET fields 2 timeout)
      chapeau_add_case(known_defect_${scenario} chapeau_probe ${scenario}
         TIMEOUT ${timeout} KNOWN_DEFECT ${id} LABEL known-defect)
   endforeach()
endif()

add_custom_target(chapeau_known_defects_report
   COMMAND ${CMAKE_COMMAND}
      "-DEXE=$<TARGET_FILE:chapeau_probe>"
      "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/case-tmp/report"
      "-DDEFECTS=${CMAKE_CURRENT_SOURCE_DIR}/known_defects.cmake"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/KnownDefectsReport.cmake"
   DEPENDS chapeau_probe
   VERBATIM
)
