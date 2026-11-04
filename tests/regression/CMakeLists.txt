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

# Runs on POSIX hosts and on native Windows with MSVC (Debug and Release). On Windows every program calls
# chapeau_harness::prepareProcess() (process_setup.h) first, and RunCase.cmake/Classify.cmake have Windows rules; see README.md,
# "Windows". Any other host is skipped.
if(NOT UNIX AND NOT (WIN32 AND MSVC))
   message(STATUS "chapeau: subprocess regression harness skipped (needs a POSIX host or native Windows with MSVC)")
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

# the supported concurrency patterns (H1a-12a); kept apart from chapeau_probe so
# that only this program needs threads
find_package(Threads REQUIRED)
add_executable(chapeau_concurrency_probe chapeau_concurrency_probe.cpp)
target_link_libraries(chapeau_concurrency_probe PRIVATE chapeau Threads::Threads)
target_include_directories(chapeau_concurrency_probe PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)

foreach(target chapeau_harness_helper chapeau_probe chapeau_concurrency_probe)
   target_compile_features(${target} PRIVATE cxx_std_20)
   target_compile_options(${target} PRIVATE
      $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra>)
endforeach()

# chapeau_add_case(<test name> <target> <scenario>
#                  [EXPECT success|exit:<n>|abort] [ABORT_DIAGNOSTIC <regex>]
#                  [TIMEOUT <seconds>]
#                  [KNOWN_DEFECT <id>] [LABEL <label>]
#                  [MATCH <regex>...] [REJECT <regex>...])
# EXPECT is success unless given; there is no way to expect a crash or a hang.
# The one exception is EXPECT abort, which needs ABORT_DIAGNOSTIC: the process
# must end with SIGABRT and print that text (see RunCase.cmake).
# The CTest TIMEOUT is only a backstop 60 s beyond the driver's own limit.
function(chapeau_add_case name target scenario)
   cmake_parse_arguments(C "" "EXPECT;TIMEOUT;KNOWN_DEFECT;LABEL;ABORT_DIAGNOSTIC" "MATCH;REJECT" ${ARGN})
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

   if(C_ABORT_DIAGNOSTIC)
      list(APPEND command "-DABORT_DIAGNOSTIC=${C_ABORT_DIAGNOSTIC}")
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
   errors_busy_is_recorded_and_recovers
   errors_eager_busy_failure)
   chapeau_add_case(regression_${scenario} chapeau_probe ${scenario}
      TIMEOUT 60 LABEL regression)
endforeach()

# the supported concurrency patterns (H1a-12a): separate connections on threads,
# a unit handed from one thread to another, the opener thread exiting, cursors
# destroyed on another thread. Each runs in a bounded process, and carries the
# label "concurrency" besides "regression" so that CI can select them without
# naming or counting them (ctest -L concurrency).
foreach(scenario
   concurrency_sqlite_threading_configuration
   concurrency_separate_connections_same_file
   concurrency_handoff_cursor_continues
   concurrency_handoff_keeps_error_snapshot_and_transaction
   concurrency_opener_thread_exits
   concurrency_cursor_destroyed_after_handoff
   concurrency_cursor_outlives_database_across_threads)
   chapeau_add_case(regression_${scenario} chapeau_concurrency_probe ${scenario}
      TIMEOUT 120 LABEL regression)
   set_property(TEST regression_${scenario} APPEND PROPERTY LABELS concurrency)
endforeach()

# refusals (H1a-12g): a call made while another call of the same unit is running is
# refused, and a destructor that meets a unit in use ends the process. The owner is
# held inside the unit by a gate function, so every case is deterministic. Labels
# "concurrency" (so that the tsan job runs them: the intruder calls must be clean
# under ThreadSanitizer, they touch nothing but atomics) and "refusal".
foreach(scenario
   refusal_next_versus_end_of_data
   refusal_accessors_while_another_thread_advances
   refusal_while_the_owner_records_an_error
   refusal_leaves_the_error_snapshot_as_the_owner_left_it
   refusal_two_simultaneous_intruders
   refusal_independent_connections_do_not_refuse_each_other
   refusal_handoff_of_a_live_cursor_is_never_refused
   refusal_leaves_the_owner_statement_alone
   refusal_cursor_outlives_database_across_threads)
   chapeau_add_case(regression_${scenario} chapeau_concurrency_probe ${scenario}
      TIMEOUT 120 LABEL regression)
   set_property(TEST regression_${scenario} APPEND PROPERTY LABELS concurrency refusal)
endforeach()

# the log line of a refusal: the first and then the powers of two (9 refusals: 1, 2, 4, 8)
chapeau_add_case(regression_refusal_logging_thresholds chapeau_concurrency_probe
   refusal_logging_thresholds
   TIMEOUT 120 LABEL regression
   MATCH
      "DB Warning [[]ConcurrentUse[]]: a call was refused because the unit is in use by another call [(]unit [0-9]+, "
      "refusal 1 on this unit"
      "refusal 2 on this unit"
      "refusal 4 on this unit"
      "refusal 8 on this unit"
   REJECT
      "refusal (3|5|6|7|9) on this unit"
      "DB Error")
set_property(TEST regression_refusal_logging_thresholds APPEND PROPERTY LABELS concurrency refusal)

# inUse() is a probe: busy is the answer, and nothing is logged even with logging on
chapeau_add_case(regression_refusal_inuse_is_a_silent_probe chapeau_concurrency_probe
   refusal_inuse_is_a_silent_probe
   TIMEOUT 120 LABEL regression
   REJECT "ConcurrentUse" "DB Error")
set_property(TEST regression_refusal_inuse_is_a_silent_probe APPEND PROPERTY LABELS concurrency refusal)

# destructors: a unit in use ends the process with a diagnostic. EXPECT abort accepts SIGABRT,
# the diagnostic, and nothing else (see RunCase.cmake); the other patterns show how far each got.
chapeau_add_case(regression_destructor_aborts_when_the_database_is_in_use_by_another_thread
   chapeau_concurrency_probe
   destructor_aborts_when_the_database_is_in_use_by_another_thread
   EXPECT abort
   ABORT_DIAGNOSTIC "Chapeau: fatal: ~SQLiteDatabase ran while its unit"
   TIMEOUT 120 LABEL regression
   MATCH "in use by a call on another thread" "destroying the database from another thread"
   REJECT "NOT REACHED" "ConcurrentUse")
chapeau_add_case(regression_destructor_aborts_when_a_cursor_is_destroyed_while_the_unit_is_in_use
   chapeau_concurrency_probe
   destructor_aborts_when_a_cursor_is_destroyed_while_the_unit_is_in_use
   EXPECT abort
   ABORT_DIAGNOSTIC "Chapeau: fatal: ~SQLiteResultSet ran while its unit"
   TIMEOUT 120 LABEL regression
   MATCH "in use by a call on another thread" "destroying a cursor from another thread"
   REJECT "NOT REACHED" "ConcurrentUse")
chapeau_add_case(regression_destructor_aborts_when_the_database_is_destroyed_from_inside_its_own_call
   chapeau_concurrency_probe
   destructor_aborts_when_the_database_is_destroyed_from_inside_its_own_call
   EXPECT abort
   ABORT_DIAGNOSTIC "Chapeau: fatal: ~SQLiteDatabase ran while its unit"
   TIMEOUT 120 LABEL regression
   MATCH "in use by an earlier call on this same thread" "destroys the database from inside"
   REJECT "NOT REACHED" "ConcurrentUse")
chapeau_add_case(regression_destructor_aborts_when_a_cursor_is_destroyed_from_inside_its_own_step
   chapeau_concurrency_probe
   destructor_aborts_when_a_cursor_is_destroyed_from_inside_its_own_step
   EXPECT abort
   ABORT_DIAGNOSTIC "Chapeau: fatal: ~SQLiteResultSet ran while its unit"
   TIMEOUT 120 LABEL regression
   MATCH "in use by an earlier call on this same thread" "destroys the result set from inside"
   REJECT "NOT REACHED" "ConcurrentUse")
foreach(scenario
   destructor_aborts_when_the_database_is_in_use_by_another_thread
   destructor_aborts_when_a_cursor_is_destroyed_while_the_unit_is_in_use
   destructor_aborts_when_the_database_is_destroyed_from_inside_its_own_call
   destructor_aborts_when_a_cursor_is_destroyed_from_inside_its_own_step)
   set_property(TEST regression_${scenario} APPEND PROPERTY LABELS concurrency refusal)
endforeach()

# the messages printed for a failed sqlite3_step: SQLite's own code and text,
# and no "Unknown error" for a code SQLite defines
chapeau_add_case(regression_diagnostics_step_error_messages chapeau_probe
   diagnostics_step_error_messages
   TIMEOUT 60 LABEL regression
   MATCH
      "DB Error [[]Sqlite[]] code=19: UNIQUE constraint failed: t[.]id in executeUpdate"
      "DB Error [[]Sqlite[]] code=19: UNIQUE constraint failed: t[.]id in executeQuery"
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

# ---- a SQLite without threading support: opt-in, needs the pinned amalgamation ----

include(${CMAKE_CURRENT_SOURCE_DIR}/threadsafe0.cmake)

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
