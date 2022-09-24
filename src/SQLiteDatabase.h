#ifndef CHAPEAU_SQLITEDATABASE_H
#define CHAPEAU_SQLITEDATABASE_H

#include <string>
#include <map>
#include <vector>

#include "sqlite3.h"
#include "Database.h"

namespace chapeau {

class SQLiteStatement;
class SQLiteResultSet;
class DBNull;
class DBBool;
class DBInt;
class DBLong;
class DBFloat;
class DBDouble;
class DBStatementArgs;
class DBString;
class DBDate;
class DBData;


class SQLiteDatabase : public Database {

public:
   SQLiteDatabase(const std::string& inPath);
   ~SQLiteDatabase();

   bool open();
   void close();
   bool goodConnection();

   bool bindData(const DBData* data, int idx, DBStatement* statement);
   bool bindDate(const DBDate* value, int idx, DBStatement* statement);
   bool bindBool(bool value, int idx, DBStatement* statement);
   bool bindInt(int value, int idx, DBStatement* statement);
   bool bindLong(long value, int idx, DBStatement* statement);
   bool bindFloat(float value, int idx, DBStatement* statement);
   bool bindDouble(double value, int idx, DBStatement* statement);
   bool bindString(const std::string& value, int idx, DBStatement* statement);
   bool bindNull(int idx, DBStatement* statement);


   std::string description() const;

   const std::string& databasePath() const;

   std::string lastErrorMessage();

   int lastErrorCode();
   bool hadError();
   long long int lastInsertRowId();

   sqlite3* sqliteHandle();

   bool executeUpdate(const std::string& sql,
                      unsigned long& rowsAffectedCount);
   bool executeUpdate(const std::string& sql,
                      const DBStatementArgs& args,
                      unsigned long& rowsAffectedCount);

   DBResultSet* executeQuery(const std::string& sql);
   DBResultSet* executeQuery(const std::string& sql,
                             const DBStatementArgs& args);

   bool rollback();
   bool commit();
   bool beginTransaction();
   bool beginDeferredTransaction();

   bool logsErrors();
   void setLogsErrors(bool flag);

   bool crashOnErrors();
   void setCrashOnErrors(bool flag);

   bool inUse();
   void setInUse(bool value);

   bool inTransaction();
   void setInTransaction(bool flag);

   bool traceExecution();
   void setTraceExecution(bool flag);

   bool checkedOut();
   void setCheckedOut(bool flag);

   int busyRetryTimeout();
   void setBusyRetryTimeout(int newBusyRetryTimeout);

   static std::string sqliteLibVersion();


protected:
   void complainAboutInUse();


private:
   sqlite3*    m_db;
   std::string m_databasePath;
   bool        m_logsErrors;
   bool        m_crashOnErrors;
   bool        m_inUse;
   bool        m_inTransaction;
   bool        m_traceExecution;
   bool        m_checkedOut;
   int         m_busyRetryTimeout;

   SQLiteDatabase();
   SQLiteDatabase(const SQLiteDatabase&);
   SQLiteDatabase& operator=(const SQLiteDatabase&);

};


class SQLiteStatement : public DBStatement {
public:
   SQLiteStatement(sqlite3_stmt* value, bool isCached);
   ~SQLiteStatement();

   void close();
   void reset();

   sqlite3_stmt* statement();

   const std::string& query() const;
   void setQuery(const std::string& value);

   long useCount() const;
   void incrementUseCount();
   bool isCached() const;

   std::string description() const;


private:
   sqlite3_stmt* m_statement;
   std::string m_query;
   long m_useCount;
   bool m_isCached;

   SQLiteStatement();
   SQLiteStatement(const SQLiteStatement&);
   SQLiteStatement& operator=(const SQLiteStatement&);

};

}

#endif

