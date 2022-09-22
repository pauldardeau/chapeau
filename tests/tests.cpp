#include "tests.h"

#include "test_database.h"
#include "test_dbdate.h"
#include "test_db_statement_args.h"
#include "test_sqlite_database.h"
#include "test_sqlite_result_set.h"
#include "test_sqlite_statement.h"


void Tests::run() {
   //TestDatabase test_database;
   //test_database.run();

   TestDBDate test_dbdate;
   test_dbdate.run();

   //TestDBStatementArgs test_db_statement_args;
   //test_db_statement_args.run();

   //TestSQLiteDatabase test_sqlite_db;
   //test_sqlite_db.run();

   //TestSQLiteResultSet test_sqlite_rs;
   //test_sqlite_rs.run();

   //TestSQLiteStatement test_sqlite_stmt;
   //test_sqlite_stmt.run();
}

int main(int argc, char* argv[]) {
   Tests tests;
   tests.run();
}
