# The known-defect inventory: one line per reproduction in chapeau_probe.cpp.
# Used by CMakeLists.txt (to register the tests) and by
# KnownDefectsReport.cmake (to print the inventory).
#
# Format: "<defect id>|<scenario>|<timeout in seconds>|<what is wrong>"
#
# The ids are those of the H1a investigation report
# (working-docs/CHAPEAU_H1A_INVESTIGATION_REPORT.md). A scenario asserts the
# behaviour a FIXED Chapeau must have, so while the defect exists it ends in a
# sanitizer report, a crash, a timeout or a failed check, and its CTest case
# fails. When a defect is fixed, move its line from CHAPEAU_DEFECT_TABLE to
# CHAPEAU_FIXED_TABLE: the scenario then becomes an ordinary regression case
# in the default test set.
#
# Hangs get a short timeout: the harness kills the child at that point.
#
# CH-08 (BUSY inside next() looks like the end of the data) is deliberately not
# in the table: fixing it needs an error-reporting API on DBResultSet that does
# not exist yet, so there is no behaviour a test could assert without choosing
# that API. The scenario characterize_ch08_busy_in_next prints what happens
# today (no assertion); once the API exists, add a real check and a table line.

set(CHAPEAU_DEFECT_TABLE
   "CH-03|ch03_noarg_overload_invalidates_cached_statement|60|no-argument overload finalizes a statement the cache still holds"
   "CH-04|ch04_two_cursors_same_sql_independent|60|two live cursors over the same SQL share one statement"
   "CH-05|ch05_next_after_end_cached|60|next() after the end restarts the query (cached)"
   "CH-05|ch05_next_after_end_uncached|60|next() after the end dereferences a null statement (uncached)"
   "CH-06|ch06_schema_change_cached_update|60|cached update statement fails permanently after a schema change"
   "CH-06|ch06_schema_change_cached_query|60|cached query statement fails permanently after a schema change"
   "CH-07|ch07_busy_prepare_hang|10|executeQuery() retries forever on a busy database (busy limit 0)"
   "CH-07|ch07_busy_next_hang|10|next() retries forever on a busy database"
   "CH-07|ch07_begin_blocked_by_open_cursor|10|beginTransaction() retries forever while a cursor is open elsewhere"
   "CH-07|ch07_commit_blocked_by_reader|10|commit() retries forever while a reader is active elsewhere"
)

# Reproductions whose defect has been fixed; run in the default test set.
set(CHAPEAU_FIXED_TABLE
   "CH-01|ch01_cursor_after_close|60|result set used after close() of its database"
   "CH-01|ch01_cursor_after_database_destroyed|60|result set destroyed after its database object"
   "CH-01|ch01_cursor_after_cache_disabled|60|result set used after the statement cache was switched off"
   "CH-02|ch02_bound_args_destroyed_before_step|60|bound arguments destroyed before the result set is stepped"
   # fixed as a consequence of the CH-01 ownership change (close() and the
   # destructor now invalidate live result sets and finalize their statements);
   # the busy-wait loops themselves (CH-07 proper) are unchanged
   "CH-07|ch07_close_with_live_cursor|10|close() retries forever with an uncached cursor alive"
   "CH-07|ch07_destructor_with_live_cursor|10|destructor retries forever with an uncached cursor alive"
)
