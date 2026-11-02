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
# Hangs get a timeout: the harness kills the child at that point.

# Reproductions of defects that are not fixed yet. Opt-in with
# -DCHAPEAU_KNOWN_DEFECTS=ON; they fail until the defect is fixed. None of the
# reproduced defects (CH-01 .. CH-08, CH-11 .. CH-13) is open any more.
set(CHAPEAU_DEFECT_TABLE
)

# Reproductions whose defect has been fixed; run in the default test set.
set(CHAPEAU_FIXED_TABLE
   "CH-01|ch01_cursor_after_close|60|result set used after close() of its database"
   "CH-01|ch01_cursor_after_database_destroyed|60|result set destroyed after its database object"
   "CH-01|ch01_cursor_after_cache_disabled|60|result set used after the statement cache was switched off"
   "CH-02|ch02_bound_args_destroyed_before_step|60|bound arguments destroyed before the result set is stepped"
   "CH-03|ch03_noarg_overload_invalidates_cached_statement|60|no-argument overload finalizes a statement the cache still holds"
   "CH-04|ch04_two_cursors_same_sql_independent|60|two live cursors over the same SQL share one statement"
   "CH-05|ch05_next_after_end_cached|60|next() after the end restarts the query (cached)"
   "CH-05|ch05_next_after_end_uncached|60|next() after the end dereferences a null statement (uncached)"
   "CH-06|ch06_schema_change_cached_update|60|cached update statement fails permanently after a schema change"
   "CH-06|ch06_schema_change_cached_query|60|cached query statement fails permanently after a schema change"
   # CH-07: with a busy limit of 0 these retried forever; the busy timeout is
   # now handled by SQLite (milliseconds, default 5000) and every wait is bounded
   "CH-07|ch07_busy_prepare_hang|60|executeQuery() retries forever on a busy database"
   "CH-07|ch07_busy_next_hang|60|next() retries forever on a busy database"
   "CH-07|ch07_begin_blocked_by_open_cursor|60|beginTransaction() retries forever while a cursor is open elsewhere"
   "CH-07|ch07_commit_blocked_by_reader|60|commit() retries forever while a reader is active elsewhere"
   # fixed earlier, by the CH-01 ownership change (close() and the destructor
   # invalidate live result sets and finalize their statements)
   "CH-07|ch07_close_with_live_cursor|10|close() retries forever with an uncached cursor alive"
   "CH-07|ch07_destructor_with_live_cursor|10|destructor retries forever with an uncached cursor alive"
   # CH-08: was a characterization with no assertion; the result set now has a
   # status, so a busy step can be asserted to be an error rather than the end
   "CH-08|ch08_busy_in_next_is_an_error|60|a BUSY error inside next() looks like the end of the data"
   # CH-11 .. CH-13 (H1a-5): the transaction state comes from SQLite, open() twice
   # is refused instead of leaking, and the statement cache is bounded (LRU)
   "CH-11|ch11_in_transaction_follows_external_commit|60|inTransaction() stale after a COMMIT run as plain SQL"
   "CH-12|ch12_open_twice_keeps_one_connection|60|a second open() leaked the first connection"
   "CH-13|ch13_statement_cache_is_bounded|60|the statement cache grows without bound"
   # CH-21 (H1a-7a): executeUpdate() refuses a statement that returns rows instead of
   # asserting
   "CH-21|ch21_select_in_executeupdate|60|executeUpdate() asserts when the statement returns rows"
   # CH-18 (H1a-9): executeQuery() runs a statement that has no result columns before
   # it returns, instead of leaving it to a next() that nobody calls
   "CH-18|ch18_query_without_columns_never_stepped|60|executeQuery() never ran INSERT/UPDATE/DELETE/DDL unless the result set was stepped"
)
