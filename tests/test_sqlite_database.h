#ifndef TEST_SQLITE_DATABASE_H
#define TEST_SQLITE_DATABASE_H

#include "TestSuite.h"


class TestSQLiteDatabase : public poivre::TestSuite {
protected:
   void runTests();

public:
   TestSQLiteDatabase();
   
};


#endif

