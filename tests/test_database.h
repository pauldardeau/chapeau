#ifndef TEST_DATABASE_H
#define TEST_DATABASE_H

#include <string>
#include "TestSuite.h"


class TestDatabase : public chaudiere::TestSuite {
protected:
   void runTests();

   void test_bind_null();
   void test_bind_bool();
   void test_bind_int();
   void test_bind_long();
   void test_bind_float();
   void test_bind_double();
   void test_bind_string();
   void test_bind_data();
   void test_bind_date();

   void test_shouldCacheStatements();
   void test_setShouldCacheStatements();
   void test_cachedStatementForQuery();
   void test_setCachedStatement();
   void test_clearCachedStatements();

   void test_resultset_query();
   void test_resultset_setQuery();

public:
   TestDatabase();
};

#endif

