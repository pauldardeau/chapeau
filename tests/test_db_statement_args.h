#ifndef TEST_DB_STATEMENT_ARGS_H
#define TEST_DB_STATEMENT_ARGS_H

#include "TestSuite.h"


class TestDBStatementArgs : public chaudiere::TestSuite {
protected:
   void runTests();

   void test_constructor();
   void test_destructor();
   void test_clear();
   void test_size();
   void test_empty();
   void test_add();
   void test_non_const_operator_brackets();
   void test_const_operator_brackets();

public:
   TestDBStatementArgs();

};


#endif

