#ifndef TEST_SQLITE_FOUNDATION_H
#define TEST_SQLITE_FOUNDATION_H

#include <string>

#include "TestSuite.h"

namespace chapeau {
class SQLiteResultSet;
}


/**
 * Positive regression tests for SQLite behaviour that is known to work
 * today. Each expected value is derived from the SQL that is run (or from
 * SQLite's documented behaviour), never from what Chapeau happened to
 * return, and no test depends on the order of unordered rows.
 *
 * Also covers the result-set and argument lifetime contract (CH-01, CH-02):
 * a result set may outlive its database, and the arguments of a query may be
 * destroyed before the result set is stepped.
 *
 * Also covers statement execution (CH-03, CH-04, CH-05): result sets over
 * identical SQL are independent, wrong argument counts are refused without
 * harm to the cached statement, and an exhausted result set stays exhausted.
 *
 * Also covers schema changes under cached statements (CH-06): a query or an
 * update prepared before an ALTER TABLE or CREATE INDEX still works, and sees
 * the new schema, afterwards.
 *
 * Also covers the busy timeout setting (CH-07) and the result-set status and
 * error code (CH-08). Lock contention itself -- which waits on purpose -- is
 * exercised in bounded subprocesses, see tests/regression/.
 */
class TestSQLiteFoundation : public poivre::TestSuite {
protected:
   void runTests();

   void test_scoped_temp_dir();
   void test_open_close();
   void test_failed_open();
   void test_create_insert_update_delete();
   void test_query_values();
   void test_parameter_binding();
   void test_zero_row_results();
   void test_transactions();
   void test_constraint_error_recovery();
   void test_cursor_after_database_close();
   void test_cursor_after_database_destruction();
   void test_cursor_with_cache_disabled();
   void test_cursor_close_and_destroy_repeated();
   void test_cursor_invalid_after_reopen();
   void test_temporary_text_arguments();
   void test_temporary_blob_arguments();
   void test_two_cursors_identical_sql();
   void test_two_cursors_parameterized_sql();
   void test_close_one_cursor_while_another_is_active();
   void test_statement_reuse_after_cursor_destroyed();
   void test_incorrect_argument_counts();
   void test_arguments_then_none_then_arguments();
   void test_cursor_exhaustion_is_terminal();
   void test_cache_disabled_with_several_active_cursors();
   void test_update_while_cursor_owns_same_sql();
   void test_schema_change_query();
   void test_schema_change_update();
   void test_schema_change_repeated();
   void test_schema_change_then_constraint_error();
   void test_schema_change_with_independent_cursors();
   void test_busy_timeout_configuration();
   void test_result_set_status_transitions();
   void test_close_with_unfinalized_statement();

   void requireLongEquals(long long expected,
                          long long actual,
                          const std::string& description);
   void requireDoubleEquals(double expected,
                            double actual,
                            const std::string& description);
   void requireCursorHasNoData(chapeau::SQLiteResultSet& resultSet,
                             const std::string& when);

public:
   TestSQLiteFoundation();
};


#endif
