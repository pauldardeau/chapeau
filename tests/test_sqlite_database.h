#ifndef TEST_SQLITE_DATABASE_H
#define TEST_SQLITE_DATABASE_H

#include "TestSuite.h"


class TestSQLiteDatabase : public poivre::TestSuite {
protected:
   void runTests();

   void test_constructor();
   void test_destructor();
   void test_sqliteLibVersion();
   void test_databasePath();
   void test_sqliteHandle();
   void test_open();
   void test_close();
   void test_goodConnection();
   void test_complainAboutInUse();
   void test_lastErrorMessage();
   void test_hadError();
   void test_lastErrorCode();
   void test_lastInsertRowId();
   void test_bindNull();
   void test_bindData();
   void test_bindDate();
   void test_bindBool();
   void test_bindInt();
   void test_bindLong();
   void test_bindFloat();
   void test_bindDouble();
   void test_bindString();
   void test_executeQuery();
   void test_executeQuery_withArgs();
   void test_executeUpdate();
   void test_executeUpdate_withArgs();
   void test_rollback();
   void test_commit();
   void test_beginDeferredTransaction();
   void test_beginTransaction();
   void test_logsErrors();
   void test_setLogsErrors();
   void test_crashOnErrors();
   void test_setCrashOnErrors();
   void test_inUse();
   void test_setInUse();
   void test_inTransaction();
   void test_setInTransaction();
   void test_traceExecution();
   void test_setTraceExecution();
   void test_checkedOut();
   void test_setCheckedOut();
   void test_busyRetryTimeout();
   void test_setBusyRetryTimeout();

public:
   TestSQLiteDatabase();
   
};


#endif

