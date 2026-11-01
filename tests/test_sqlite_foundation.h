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
 * Deliberately NOT covered here: a statement shared by two cursors, next()
 * after the end, schema changes after a statement was cached, and busy-wait
 * behaviour. Those are known defects; see tests/regression/.
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
