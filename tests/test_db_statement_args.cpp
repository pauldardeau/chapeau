#include "test_db_statement_args.h"
#include "DBStatementArgs.h"


TestDBStatementArgs::TestDBStatementArgs() :
   TestSuite("TestDBStatementArgs") {
}

void TestDBStatementArgs::runTests() {
   test_constructor();
   test_destructor();
   test_clear();
   test_size();
   test_empty();
   test_add();
   test_non_const_operator_brackets();
   test_const_operator_brackets();
}

void TestDBStatementArgs::test_constructor() {
   TEST_CASE("test_constructor");
}

void TestDBStatementArgs::test_destructor() {
   TEST_CASE("test_destructor");
}

void TestDBStatementArgs::test_clear() {
   TEST_CASE("test_clear");
}

void TestDBStatementArgs::test_size() {
   TEST_CASE("test_size");
}

void TestDBStatementArgs::test_empty() {
   TEST_CASE("test_empty");
}

void TestDBStatementArgs::test_add() {
   TEST_CASE("test_add");
}

void TestDBStatementArgs::test_non_const_operator_brackets() {
   TEST_CASE("test_non_const_operator_brackets");
}

void TestDBStatementArgs::test_const_operator_brackets() {
   TEST_CASE("test_const_operator_brackets");
}

