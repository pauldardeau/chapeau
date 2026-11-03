#ifndef TEST_UNIT_CLAIMS_H
#define TEST_UNIT_CLAIMS_H

#include <cstdint>
#include <string>

#include "SQLiteDatabase.h"
#include "SQLiteResultSet.h"
#include "TestSuite.h"


/**
 * Tests of the claims that the public entry points of SQLiteDatabase, of the
 * statement cache and of SQLiteResultSet take (H1a-12g): what a call that is made
 * while another call of the same unit is running gets (a refusal: a neutral value,
 * nothing touched, a record), and that no ordinary call is ever refused.
 *
 * The final-hardening tests (test_nested_refusal_*) also say what an outer call
 * leaves behind when a call was refused from inside it: its own result on the error
 * snapshot and on its cursor, never the InUse of the refused call. The one test
 * here that uses a second thread (a cross-thread refusal reports nothing to the
 * snapshot) holds the claim on the test thread as a stand-in and joins the other.
 *
 * Everything else here runs on ONE thread. A refusal needs a call to be running when
 * another is made, and a SQLite user function (a callback that runs inside
 * executeUpdate() or inside a step) provides exactly that, deterministically, with
 * no timing: the call made from the function is a re-entrant call, which the claim
 * refuses like it refuses another thread's. The cases that need two threads are
 * subprocess scenarios (tests/regression/chapeau_concurrency_probe.cpp,
 * "refusal_*"), where a hang or a crash cannot take this suite down.
 */
class TestUnitClaims : public poivre::TestSuite {
protected:
   void runTests();

   void requireLongEquals(long long expected, long long actual, const std::string& description);

   void test_ordinary_use_is_never_refused();
   void test_every_database_entry_point_refuses_inside_a_call();
   void test_every_cursor_entry_point_refuses_inside_a_call();
   void test_a_refused_next_is_not_the_end_of_the_data();
   void test_a_refusal_changes_nothing_of_the_running_call();
   void test_reentrance_from_a_cursor_step_is_refused_for_the_database();
   void test_inuse_is_a_probe();
   void test_refusals_are_logged_at_power_of_two_counts();
   void test_refusals_are_silent_when_logging_is_off();
   void test_a_cursor_outlives_its_database_and_still_claims();
   void test_sequential_transaction_interleaving_is_not_detected();

   // What a callback does from inside an outer call: three calls on the unit, each of
   // which must be refused, one of which would change the data if it ran.
   struct NestedCalls {
      bool update;
      unsigned long rows;
      chapeau::DBResultSet* query;
      long long cursor;
      chapeau::DBErrorKind kindInside;
      NestedCalls() : update(true), rows(99), query(reinterpret_cast<chapeau::DBResultSet*>(1)),
                      cursor(-2), kindInside(chapeau::DBErrorKind::None) {}
      void run(chapeau::SQLiteDatabase& db, chapeau::SQLiteResultSet* sibling);
   };
   static const long NESTED_REFUSALS = 3;

   // The nested calls were refused, counted (unit and thread), did not run, and reported
   // InUse while the outer call was still running. Runs a query of its own at the end,
   // so the caller reads the error snapshot BEFORE calling it.
   void requireNestedCallsRefused(const NestedCalls& seen, chapeau::SQLiteDatabase& db,
                                  uint64_t unitBefore, uint64_t threadBefore, const std::string& scenario);

   // H1a-12g final hardening: a call refused from inside an outer call, and what the
   // outer call leaves on the database's error snapshot and on its cursor
   void test_nested_refusal_outer_query_succeeds();
   void test_nested_refusal_outer_query_fails();
   void test_nested_refusal_outer_update_succeeds();
   void test_nested_refusal_outer_update_fails();
   void test_nested_refusal_outer_next_succeeds();
   void test_nested_refusal_outer_next_fails();
   void test_nested_refusal_outer_transaction_calls();
   void test_nested_refusal_does_not_hide_a_failure_of_an_earlier_call();
   void test_nested_refusal_outer_good_connection_succeeds();
   void test_a_note_of_a_nested_refusal_does_not_leak_into_a_later_call();
   void test_cross_thread_refusals_never_touch_the_error_snapshot();

public:
   TestUnitClaims();

};

#endif
