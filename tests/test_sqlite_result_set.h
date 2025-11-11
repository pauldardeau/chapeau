#ifndef TEST_SQLITE_RESULT_SET_H
#define TEST_SQLITE_RESULT_SET_H

#include "TestSuite.h"


class TestSQLiteResultSet : public poivre::TestSuite {
protected:
   void runTests();

   void test_constructor();
   void test_destructor();
   void test_close();
   void test_columnIndexForName();
   void test_statement();
   void test_setStatement();
   void test_setParentDB();
   void test_next();
   void test_intForColumn();
   void test_intForColumnIndex();
   void test_longForColumn();
   void test_longForColumnIndex();
   void test_boolForColumn();
   void test_boolForColumnIndex();
   void test_doubleForColumn();
   void test_doubleForColumnIndex();
   void test_stringForColumn();
   void test_stringForColumnIndex();
   void test_dateForColumn();
   void test_dateForColumnIndex();
   void test_dataForColumn();
   void test_dataForColumnIndex();
   void test_setupColumnNames();

public:
   TestSQLiteResultSet();

};


#endif

