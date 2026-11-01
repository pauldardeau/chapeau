#include "tests.h"

#include <stdio.h>
#include <string>
#include <vector>

#include "TestRegistry.h"
#include "TestSuite.h"

#include "test_database.h"
#include "test_dbdate.h"
#include "test_db_statement_args.h"
#include "test_sqlite_database.h"
#include "test_sqlite_result_set.h"
#include "test_sqlite_statement.h"


namespace {

// totals over the classic suites run below; POIVRE_TEST_CASE cases (none
// today) are counted separately by the TestRegistry, so nothing is counted
// twice
struct ClassicTotals {
   int suitesRun = 0;
   int testCasesRun = 0;
   int assertions = 0;
   int failures = 0;
   std::vector<std::string> failedSuites;

   void add(const poivre::TestSuite& suite) {
      ++suitesRun;
      testCasesRun += suite.getNumTestCasesRun();
      assertions += suite.getNumTestsExecuted();
      failures += suite.getNumFailures();
      if (suite.getNumFailures() > 0) {
         failedSuites.push_back(suite.getName());
      }
   }
};

}

int Tests::run() {
   ClassicTotals totals;

   TestDatabase test_database;
   test_database.run();
   totals.add(test_database);

   TestDBDate test_dbdate;
   test_dbdate.run();
   totals.add(test_dbdate);

   TestDBStatementArgs test_db_statement_args;
   test_db_statement_args.run();
   totals.add(test_db_statement_args);

   TestSQLiteDatabase test_sqlite_db;
   test_sqlite_db.run();
   totals.add(test_sqlite_db);

   TestSQLiteResultSet test_sqlite_rs;
   test_sqlite_rs.run();
   totals.add(test_sqlite_rs);

   TestSQLiteStatement test_sqlite_stmt;
   test_sqlite_stmt.run();
   totals.add(test_sqlite_stmt);

   // self-registering POIVRE_TEST_CASE cases: there are none today, so the
   // registry is only run when something has registered. (Running an empty
   // registry would add nothing but a misleading "no test cases were
   // executed" line; a genuinely empty overall run is still caught below,
   // because the totals then stay at zero.)
   poivre::TestRegistry& registry = poivre::TestRegistry::instance();
   int registeredTestCases = 0;
   int registeredAssertions = 0;
   int registeredFailures = 0;
   if (registry.getNumRegisteredTestCases() > 0) {
      registry.runAll();
      registeredTestCases = registry.getNumTestCasesRun();
      registeredAssertions = registry.getNumAssertions();
      registeredFailures = registry.getNumFailures();
   }

   const int testCases = totals.testCasesRun + registeredTestCases;
   const int assertions = totals.assertions + registeredAssertions;
   const int failures = totals.failures + registeredFailures;

   printf("\n=============================================\n");
   printf("test_chapeau overall: %d classic suite(s), %d test case(s), "
          "%d assertion(s) executed, %d failed\n",
          totals.suitesRun, testCases, assertions, failures);
   for (const std::string& name : totals.failedSuites) {
      printf("*** failed suite: %s\n", name.c_str());
   }
   if (testCases == 0 || assertions == 0) {
      printf("*** no tests or assertions were executed; treated as a failure\n");
   }
   if (registeredFailures > 0) {
      printf("*** %d failure(s) in registered POIVRE_TEST_CASE cases\n",
             registeredFailures);
   }
   printf("=============================================\n");
   fflush(stdout);

   // 0 only if at least one assertion ran and nothing failed; never the raw
   // failure count (exit statuses are truncated: 256 failures would be 0)
   return poivre::TestRegistry::exitCodeFor(testCases, assertions, failures);
}

int main(int argc, char* argv[]) {
   Tests tests;
   return tests.run();
}
