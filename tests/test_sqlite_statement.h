#ifndef TEST_SQLITE_STATEMENT_H
#define TEST_SQLITE_STATEMENT_H

#include "TestSuite.h"


class TestSQLiteStatement : public chaudiere::TestSuite {
protected:
   void runTests();

   void test_constructor();
   void test_destructor();
   void test_close();
   void test_reset();
   void test_statement();
   void test_query();
   void test_setQuery();
   void test_useCount();
   void test_incrementUseCount();
   void test_description();

public:
   TestSQLiteStatement();

};


#endif

