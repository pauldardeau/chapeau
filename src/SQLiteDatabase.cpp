//SQLiteDatabase.cpp

#include <stdio.h>
#include <unistd.h>
#include <assert.h>

#include "SQLiteDatabase.h"
#include "SQLiteResultSet.h"
#include "DBAbstractDataType.h"
#include "DBNull.h"
#include "DBBool.h"
#include "DBInt.h"
#include "DBLong.h"
#include "DBFloat.h"
#include "DBDouble.h"
#include "DBStatementArgs.h"
#include "DBString.h"
#include "DBDate.h"
#include "DBData.h"
#include "Logger.h"


using namespace chaudiere;
using namespace chapeau;

class MarkInUse {
public:
   MarkInUse(SQLiteDatabase* db) :
      m_db(db) {
      m_db->setInUse(true);
   }
   ~MarkInUse() {
      m_db->setInUse(false);
   }

private:
   SQLiteDatabase* m_db;

   //disallow copies
   MarkInUse(const MarkInUse&);
   MarkInUse& operator=(const MarkInUse&);
};

//******************************************************************************

SQLiteDatabase::SQLiteDatabase(const std::string& aPath) :
   m_db(NULL),
   m_databasePath(aPath),
   m_logsErrors(false),
   m_crashOnErrors(false),
   m_inUse(false),
   m_inTransaction(false),
   m_traceExecution(false),
   m_checkedOut(false),
   m_busyRetryTimeout(0) {
}

//******************************************************************************

SQLiteDatabase::~SQLiteDatabase() {
   clearCachedStatements();
   close();
}

//******************************************************************************

std::string SQLiteDatabase::sqliteLibVersion() {
   return std::string(::sqlite3_libversion());
}

//******************************************************************************

const std::string& SQLiteDatabase::databasePath() const {
   return m_databasePath;
}

//******************************************************************************

sqlite3* SQLiteDatabase::sqliteHandle() {
   return m_db;
}

//******************************************************************************

bool SQLiteDatabase::open() {
   Logger::debug("opening database '" + m_databasePath + "'");
   const int err = ::sqlite3_open(m_databasePath.c_str(), &m_db);
   if (err != SQLITE_OK) {
      Logger::error("unable to open database '" + m_databasePath + "'");
      ::printf("rc=%d\n", err);
      return false;
   }

   return true;
}

//******************************************************************************

void SQLiteDatabase::close() {
   clearCachedStatements();
   
   if (!m_db) {
      return;
   }
   
   int rc;
   bool retry;
   int numberOfRetries = 0;
   do {
      retry = false;
      rc = ::sqlite3_close(m_db);
      if (SQLITE_BUSY == rc) {
         retry = true;
         usleep(20);
         if (m_busyRetryTimeout && (numberOfRetries++ > m_busyRetryTimeout)) {
            ::printf("%s:%d\n", __FUNCTION__, __LINE__);
            Logger::warning("Database busy, unable to close");
            return;
         }
      } else if (SQLITE_OK != rc) {
         ::printf("error closing!: %d\n", rc);
      }
   } while (retry);
   
   m_db = NULL;
}

//******************************************************************************

bool SQLiteDatabase::goodConnection() {
   if (!m_db) {
      return false;
   }
   
   DBResultSet* rs =
      executeQuery("select name from sqlite_master where type='table'");
   
   if (rs) {
      rs->close();
      return true;
   }
   
   return false;
}

//******************************************************************************

void SQLiteDatabase::complainAboutInUse() {
   Logger::warning("The SQLiteDatabase is currently in use.");
   
   if (m_crashOnErrors) {
   }
}

//******************************************************************************

std::string SQLiteDatabase::lastErrorMessage() {
   return std::string(::sqlite3_errmsg(m_db));
}

//******************************************************************************

bool SQLiteDatabase::hadError() {
   return (lastErrorCode() != SQLITE_OK);
}

//******************************************************************************

int SQLiteDatabase::lastErrorCode() {
   return ::sqlite3_errcode(m_db);
}

//******************************************************************************

long long int SQLiteDatabase::lastInsertRowId() {
   if (m_inUse) {
      complainAboutInUse();
      return -1;
   }
  
   MarkInUse inuse(this); 
   return ::sqlite3_last_insert_rowid(m_db);
}

//******************************************************************************

bool SQLiteDatabase::bindNull(int idx, DBStatement* statement) {
   bool bindSuccess = false;
   SQLiteStatement* stmt = dynamic_cast<SQLiteStatement*>(statement);
   if (stmt) {
      const int rc = ::sqlite3_bind_null(stmt->statement(), idx);
      if (rc == SQLITE_OK) {
         bindSuccess = true;
      } else {
         
      }
   }
   
   return bindSuccess;
}

//******************************************************************************

bool SQLiteDatabase::bindData(const DBData* data, int idx, DBStatement* statement) {
   bool bindSuccess = false;
   SQLiteStatement* stmt = dynamic_cast<SQLiteStatement*>(statement);
   if (stmt) {
      const int rc = ::sqlite3_bind_blob(stmt->statement(),
                        idx,
                        data->value(),
                        (int) data->length(),
                        SQLITE_STATIC);
      if (rc == SQLITE_OK) {
         bindSuccess = true;                  
      } else {
         
      }
   }
   
   return bindSuccess;
}

//******************************************************************************

bool SQLiteDatabase::bindDate(const DBDate* value, int idx, DBStatement* statement) {
   bool bindSuccess = false;
   SQLiteStatement* stmt = dynamic_cast<SQLiteStatement*>(statement);
   if (stmt) {
      const int rc = ::sqlite3_bind_double(stmt->statement(),
                          idx,
                          value->timeIntervalSince1970());
      if (rc == SQLITE_OK) {
         bindSuccess = true;                    
      } else {
         
      }
   }
   
   return bindSuccess;
}

//******************************************************************************

bool SQLiteDatabase::bindBool(bool value, int idx, DBStatement* statement) {
   bool bindSuccess = false;
   SQLiteStatement* stmt = dynamic_cast<SQLiteStatement*>(statement);
   if (stmt) {
      const int rc = ::sqlite3_bind_int(stmt->statement(), idx, (value ? 1 : 0));
      if (rc == SQLITE_OK) {
         bindSuccess = true;
      } else {
         
      }
   } else {
      ::printf("error: bindBool failure (no statement)\n");
   }
   
   return bindSuccess;
}

//******************************************************************************

bool SQLiteDatabase::bindInt(int value, int idx, DBStatement* statement) {
   bool bindSuccess = false;
   SQLiteStatement* stmt = dynamic_cast<SQLiteStatement*>(statement);
   if (stmt) {
      const int rc = ::sqlite3_bind_int64(stmt->statement(), idx, value);
      if (rc == SQLITE_OK) {
         bindSuccess = true;
      } else {
         
      }
   }
   
   return bindSuccess;
}

//******************************************************************************

bool SQLiteDatabase::bindLong(long value, int idx, DBStatement* statement) {
   bool bindSuccess = false;
   SQLiteStatement* stmt = dynamic_cast<SQLiteStatement*>(statement);
   if (stmt) {
      const int rc = ::sqlite3_bind_int64(stmt->statement(), idx, value);
      if (rc == SQLITE_OK) {
         bindSuccess = true;
      } else {
         
      }
   }
   
   return bindSuccess;
}

//******************************************************************************

bool SQLiteDatabase::bindFloat(float value, int idx, DBStatement* statement) {
   bool bindSuccess = false;
   SQLiteStatement* stmt = dynamic_cast<SQLiteStatement*>(statement);
   if (stmt) {
      const int rc = ::sqlite3_bind_double(stmt->statement(), idx, value);
      if (rc == SQLITE_OK) {
         bindSuccess = true;
      } else {
         
      }
   }
   
   return bindSuccess;
}

//******************************************************************************

bool SQLiteDatabase::bindDouble(double value, int idx, DBStatement* statement) {
   bool bindSuccess = false;
   SQLiteStatement* stmt = dynamic_cast<SQLiteStatement*>(statement);
   if (stmt) {
      const int rc = ::sqlite3_bind_double(stmt->statement(), idx, value);
      if (rc == SQLITE_OK) {
         bindSuccess = true;
      } else {
         
      }
   }
   
   return bindSuccess;
}

//******************************************************************************

bool SQLiteDatabase::bindString(const std::string& value,
                            int idx,
                            DBStatement* statement) {
   bool bindSuccess = false;
   SQLiteStatement* stmt =
      dynamic_cast<SQLiteStatement*>(statement);
   
   if (stmt && stmt->statement()) {
      const int rc = ::sqlite3_bind_text(stmt->statement(),
                        idx,
                        value.c_str(),
                        value.length(),
                        SQLITE_STATIC);
      if (rc == SQLITE_OK) {
         bindSuccess = true;                  
      } else {
         ::printf("sqlite3_bind_text failed, rc = %d (%s)\n",
                  rc, ::sqlite3_errmsg(m_db)); 
      }
   } else {
      ::printf("bindString failure, no statement\n");
   }
   
   return bindSuccess;
}

//******************************************************************************

DBResultSet* SQLiteDatabase::executeQuery(const std::string& sql) {
   if (NULL == m_db) {
      ::printf("error: no database connection\n");
      return NULL;
   }
   
   if (m_inUse) {
      complainAboutInUse();
      return NULL;
   }
  
   MarkInUse inuse(this); 
   
   int rc = 0;
   sqlite3_stmt* pStmt = NULL;
   SQLiteStatement* statement = NULL;
   
   if (m_traceExecution && !sql.empty()) {
      ::printf("executeQuery: %s (%s)\n", sql.c_str(), m_databasePath.c_str());
   }
   
   const bool cacheStatements = shouldCacheStatements();
   
   if (cacheStatements) {
      statement = dynamic_cast<SQLiteStatement*>(cachedStatementForQuery(sql));
      pStmt = statement ? statement->statement() : NULL;
   }
   
   int numberOfRetries = 0;
   bool retry = false;
   
   if (!pStmt) {
      do {
         retry = false;
         rc = ::sqlite3_prepare(m_db, sql.c_str(), -1, &pStmt, 0);
         
         if (SQLITE_BUSY == rc) {
            retry = true;
            usleep(20);
            
            if (m_busyRetryTimeout && (numberOfRetries++ > m_busyRetryTimeout)) {
               ::printf("%s:%d Database busy (%s)\n",
                        __FUNCTION__,
                        __LINE__,
                        m_databasePath.c_str());
               Logger::warning("Database busy");
               ::sqlite3_finalize(pStmt);
               return NULL;
            }
         } else if (SQLITE_OK != rc) {
            
            ::sqlite3_finalize(pStmt);
            
            if (m_logsErrors) {
               ::printf("DB Error: %d \"%s\"\n",
                        lastErrorCode(),
                        lastErrorMessage().c_str());
               ::printf("DB Query: %s\n", sql.c_str());
               if (m_crashOnErrors) {
               }
            }
            
            return NULL;
         }
      } while (retry);
   }
   
   const int queryCount = ::sqlite3_bind_parameter_count(pStmt);
   
   if (0 != queryCount) {
      Logger::error("the bind count is not correct for the # of variables (executeQuery)");
      ::sqlite3_finalize(pStmt);
      return NULL;
   }
   
   if (!statement) {
      statement = new SQLiteStatement(pStmt);
      
      if (cacheStatements) {
         setCachedStatement(statement,sql);
      }
   }

   statement->incrementUseCount();

   // the statement gets closed in rs dtor or close method
   return new SQLiteResultSet(statement, this, sql);
}

//******************************************************************************

DBResultSet* SQLiteDatabase::executeQuery(const std::string& sql,
                                          const DBStatementArgs& args) {
   if (NULL == m_db) {
      ::printf("error: no database connection\n");
      return NULL;
   }

   if (m_inUse) {
      complainAboutInUse();
      return NULL;
   }
   
   setInUse(true);
   
   int rc = 0;
   sqlite3_stmt* pStmt = NULL;
   SQLiteStatement* statement = NULL;
   
   if (m_traceExecution && !sql.empty()) {
      ::printf("executeQuery: %s (%s)\n",
               sql.c_str(),
               m_databasePath.c_str());
   }
   
   const bool cacheStatements = shouldCacheStatements();
   
   if (cacheStatements) {
      statement = dynamic_cast<SQLiteStatement*>(cachedStatementForQuery(sql));
      pStmt = statement ? statement->statement() : NULL;
   }
   
   int numberOfRetries = 0;
   bool retry = false;
   
   if (!pStmt) {
      do {
         retry = false;
         rc = ::sqlite3_prepare(m_db, sql.c_str(), -1, &pStmt, 0);
         
         if (SQLITE_BUSY == rc) {
            retry = true;
            usleep(20);
            
            if (m_busyRetryTimeout && (numberOfRetries++ > m_busyRetryTimeout)) {
               ::printf("%s:%d Database busy (%s)\n",
                        __FUNCTION__,
                        __LINE__,
                        m_databasePath.c_str());
               Logger::warning("Database busy");
               ::sqlite3_finalize(pStmt);
               setInUse(false);
               return NULL;
            }
         } else if (SQLITE_OK != rc) {
            ::sqlite3_finalize(pStmt);
            
            if (m_logsErrors) {
               ::printf("DB Error: %d \"%s\"\n",
                        lastErrorCode(),
                        lastErrorMessage().c_str());
               ::printf("DB Query: %s\n", sql.c_str());
               if (m_crashOnErrors) {
               }
            }
            
            setInUse(false);
            return NULL;
         }
      } while (retry);
   }
   
   const int queryCount = ::sqlite3_bind_parameter_count(pStmt);
   const size_t argCount = args.size();
   
   if (!statement) {
      statement = new SQLiteStatement(pStmt);
      
      if (cacheStatements) {
         setCachedStatement(statement, sql);
      }
   }

   int idx = 0;
   bool bindRC;

   for (; idx < argCount; ++idx) {
      const DBAbstractDataType* arg = args[idx];
      
      if (arg != NULL) {
         if (m_traceExecution) {
            ::printf("obj: %s (%s)\n",
                     arg->valueAsString().c_str(),
                     arg->typeName().c_str());
         }

         bindRC = arg->bind(this, idx+1, statement);
      } else {
         if (m_traceExecution) {
            ::printf("obj: <null>\n");
         }

         bindRC = bindNull(idx+1, statement);
      }
   }
   
   if (idx != queryCount) {
      Logger::error("the bind count is not correct for the # of variables (executeQuery)");
      ::sqlite3_finalize(pStmt);
      setInUse(false);
      return NULL;
   }

   statement->incrementUseCount();

   // the statement gets closed in rs dtor or close method
   return new SQLiteResultSet(statement, this, sql);
}

//******************************************************************************

bool SQLiteDatabase::executeUpdate(const std::string& sql) {
   if (NULL == m_db) {
      ::printf("error: no database connection\n");
      return NULL;
   }

   if (m_inUse) {
      complainAboutInUse();
      return false;
   }
   
   setInUse(true);
   
   int rc = 0;
   sqlite3_stmt* pStmt = NULL;
   SQLiteStatement* cachedStmt = NULL;
   
   if (m_traceExecution && !sql.empty()) {
      ::printf("executeUpdate: %s (%s)\n",
               sql.c_str(),
               m_databasePath.c_str());
   }
   
   bool cacheStatements = shouldCacheStatements();
   
   if (cacheStatements) {
      cachedStmt = dynamic_cast<SQLiteStatement*>(cachedStatementForQuery(sql));
      pStmt = cachedStmt ? cachedStmt->statement() : NULL;
   }
   
   int numberOfRetries = 0;
   bool retry = false;
   
   if (!pStmt) {
      do {
         retry = false;
         rc = ::sqlite3_prepare(m_db, sql.c_str(), -1, &pStmt, 0);
         if (SQLITE_BUSY == rc) {
            retry = true;
            usleep(20);
            
            if (m_busyRetryTimeout && (numberOfRetries++ > m_busyRetryTimeout)) {
               ::printf("%s:%d Database busy (%s)\n",
                        __FUNCTION__,
                        __LINE__,
                        m_databasePath.c_str());
               Logger::warning("Database busy");
               ::sqlite3_finalize(pStmt);
               setInUse(false);
               return false;
            }
         } else if (SQLITE_OK != rc) {
            ::sqlite3_finalize(pStmt);
            
            if (m_logsErrors) {
               ::printf("DB Error: %d \"%s\"\n",
                        lastErrorCode(),
                        lastErrorMessage().c_str());
               ::printf("DB Query: %s\n", sql.c_str());
               if (m_crashOnErrors) {
               }
            }
            
            setInUse(false);
            
            return false;
         }
      } while (retry);
   }
   
   const int queryCount = ::sqlite3_bind_parameter_count(pStmt);
   
   if (0 != queryCount) {
      ::printf("Error: the bind count is not correct for the # of variables (%s) (executeUpdate)\n",
               sql.c_str());
      ::sqlite3_finalize(pStmt);
      setInUse(false);
      return false;
   }
   
   /* Call sqlite3_step() to run the virtual machine. Since the SQL being
    ** executed is not a SELECT statement, we assume no data will be returned.
    */
   numberOfRetries = 0;
   do {
      rc = ::sqlite3_step(pStmt);
      retry = false;
      
      if (SQLITE_BUSY == rc) {
         // this will happen if the db is locked, like if we are doing an update or insert.
         // in that case, retry the step... and maybe wait just 10 milliseconds.
         retry = true;
         usleep(20);
         
         if (m_busyRetryTimeout && (numberOfRetries++ > m_busyRetryTimeout)) {
            ::printf("%s:%d Database busy (%s)\n",
                     __FUNCTION__,
                     __LINE__,
                     m_databasePath.c_str());
            Logger::warning("Database busy");
            retry = false;
         }
      } else if (SQLITE_DONE == rc || SQLITE_ROW == rc) {
         // all is well, let's return.
      } else if (SQLITE_ERROR == rc) {
         ::printf("Error calling sqlite3_step (%d: %s) SQLITE_ERROR\n",
                  rc,
                  ::sqlite3_errmsg(m_db));
         ::printf("DB Query: %s\n", sql.c_str());
      } else if (SQLITE_MISUSE == rc) {
         ::printf("Error calling sqlite3_step (%d: %s) SQLITE_MISUSE\n",
                  rc,
                  ::sqlite3_errmsg(m_db));
         ::printf("DB Query: %s\n", sql.c_str());
      } else {
         ::printf("Unknown error calling sqlite3_step (%d: %s) eu\n",
                  rc,
                  ::sqlite3_errmsg(m_db));
         ::printf("DB Query: %s\n", sql.c_str());
      }
      
   } while (retry);
   
   assert(rc != SQLITE_ROW);
   
   if (cacheStatements && !cachedStmt) {
      cachedStmt = new SQLiteStatement(pStmt);
      setCachedStatement(cachedStmt, sql);
   }
   
   if (cachedStmt) {
      cachedStmt->incrementUseCount();
      rc = ::sqlite3_reset(pStmt);
   } else {
      /* Finalize the virtual machine. This releases all memory and other
       ** resources allocated by the sqlite3_prepare() call above.
       */
      rc = ::sqlite3_finalize(pStmt);
   }
   
   setInUse(false);
   
   return (rc == SQLITE_OK);
}

//******************************************************************************

bool SQLiteDatabase::executeUpdate(const std::string& sql,
                                   const DBStatementArgs& args) {
   if (NULL == m_db) {
      ::printf("error: no database connection\n");
      return NULL;
   }

   if (m_inUse) {
      complainAboutInUse();
      return false;
   }
  
   MarkInUse inuse(this); 
   
   int rc = 0;
   sqlite3_stmt* pStmt = NULL;
   SQLiteStatement* cachedStmt = NULL;
   
   const bool cacheStatements = shouldCacheStatements();
   
   if (m_traceExecution && !sql.empty()) {
      ::printf("executeUpdate: %s (%s)\n",
               sql.c_str(),
               m_databasePath.c_str());
   }
   
   if (cacheStatements) {
      cachedStmt = dynamic_cast<SQLiteStatement*>(cachedStatementForQuery(sql));
      pStmt = cachedStmt ? cachedStmt->statement() : NULL;
   }
   
   int numberOfRetries = 0;
   bool retry = false;
   
   if (!pStmt) {
      do {
         retry = false;
         rc = ::sqlite3_prepare(m_db, sql.c_str(), -1, &pStmt, 0);
         if (SQLITE_BUSY == rc) {
            retry = true;
            usleep(20);
            
            if (m_busyRetryTimeout && (numberOfRetries++ > m_busyRetryTimeout)) {
               ::printf("%s:%d Database busy (%s)\n",
                        __FUNCTION__,
                        __LINE__,
                        m_databasePath.c_str());
               Logger::warning("Database busy");
               ::sqlite3_finalize(pStmt);
               return false;
            }
         } else if (SQLITE_OK != rc) {
            ::sqlite3_finalize(pStmt);
            
            if (m_logsErrors) {
               ::printf("DB Error: %d \"%s\"\n",
                        lastErrorCode(),
                        lastErrorMessage().c_str());
               ::printf("DB Query: %s\n", sql.c_str());
               if (m_crashOnErrors) {
               }
            }
            
            return false;
         }
      } while (retry);
   }
   
   const int queryCount = ::sqlite3_bind_parameter_count(pStmt);
   const size_t argsCount = args.size();
   
   if (!cachedStmt) {
      cachedStmt = new SQLiteStatement(pStmt);
      
      if (cacheStatements) {
         setCachedStatement(cachedStmt, sql);
      }
   }

   bool bindRC;
   int idx = 0;
   
   for (; idx < argsCount; ++idx) {
      const DBAbstractDataType* arg = args[idx];
      
      if (arg != NULL) {
         if (m_traceExecution) {
            ::printf("obj: %s (%s)\n",
                     arg->valueAsString().c_str(),
                     arg->typeName().c_str());
         }

         bindRC = arg->bind(this, idx+1, cachedStmt);
      } else {
         if (m_traceExecution) {
            ::printf("obj: <null>\n");
         }

         bindRC = bindNull(idx+1, cachedStmt);
      }
   }
   
   if (idx != queryCount) {
      ::printf("Error: the bind count is not correct for the # of variables (%s) (executeUpdate)\n",
               sql.c_str());
      ::sqlite3_finalize(pStmt);
      return false;
   }
   
   /* Call sqlite3_step() to run the virtual machine. Since the SQL being
    ** executed is not a SELECT statement, we assume no data will be returned.
    */
   numberOfRetries = 0;
   do {
      rc = ::sqlite3_step(pStmt);
      retry = false;
      
      if (SQLITE_BUSY == rc) {
         // this will happen if the db is locked, like if we are doing an update or insert.
         // in that case, retry the step... and maybe wait just 10 milliseconds.
         retry = true;
         usleep(20);
         
         if (m_busyRetryTimeout && (numberOfRetries++ > m_busyRetryTimeout)) {
            ::printf("%s:%d Database busy (%s)\n",
                     __FUNCTION__,
                     __LINE__,
                     m_databasePath.c_str());
            Logger::warning("Database busy");
            retry = false;
         }
      } else if (SQLITE_DONE == rc || SQLITE_ROW == rc) {
         // all is well, let's return.
      } else if (SQLITE_ERROR == rc) {
         ::printf("Error calling sqlite3_step (%d: %s) SQLITE_ERROR\n",
                  rc,
                  ::sqlite3_errmsg(m_db));
         ::printf("DB Query: %s\n", sql.c_str());
      } else if (SQLITE_MISUSE == rc) {
         ::printf("Error calling sqlite3_step (%d: %s) SQLITE_MISUSE\n",
                  rc,
                  ::sqlite3_errmsg(m_db));
         ::printf("DB Query: %s\n", sql.c_str());
      } else {
         ::printf("Unknown error calling sqlite3_step (%d: %s) eu\n",
                  rc,
                  ::sqlite3_errmsg(m_db));
         ::printf("DB Query: %s\n", sql.c_str());
      }
      
   } while (retry);
   
   assert(rc != SQLITE_ROW);
   
   if (cachedStmt) {
      cachedStmt->incrementUseCount();
      rc = ::sqlite3_reset(pStmt);
   } else {
      /* Finalize the virtual machine. This releases all memory and other
       ** resources allocated by the sqlite3_prepare() call above.
       */
      rc = ::sqlite3_finalize(pStmt);
   }
   
   return (rc == SQLITE_OK);
}

//******************************************************************************

bool SQLiteDatabase::rollback() {
   const bool b = executeUpdate("ROLLBACK TRANSACTION;");
   if (b) {
      m_inTransaction = false;
   }
   
   return b;
}

//******************************************************************************

bool SQLiteDatabase::commit() {
   const bool b = executeUpdate("COMMIT TRANSACTION;");
   if (b) {
      m_inTransaction = false;
   }
   
   return b;
}

//******************************************************************************

bool SQLiteDatabase::beginDeferredTransaction() {
   const bool b = executeUpdate("BEGIN DEFERRED TRANSACTION;");
   if (b) {
      m_inTransaction = true;
   }
   
   return b;
}

//******************************************************************************

bool SQLiteDatabase::beginTransaction() {
   const bool b = executeUpdate("BEGIN EXCLUSIVE TRANSACTION;");
   if (b) {
      m_inTransaction = true;
   }
   
   return b;
}

//******************************************************************************

bool SQLiteDatabase::logsErrors() {
   return m_logsErrors;
}

//******************************************************************************

void SQLiteDatabase::setLogsErrors(bool flag) {
   m_logsErrors = flag;
}

//******************************************************************************

bool SQLiteDatabase::crashOnErrors() {
   return m_crashOnErrors;
}

//******************************************************************************

void SQLiteDatabase::setCrashOnErrors(bool flag) {
   m_crashOnErrors = flag;
}

//******************************************************************************

bool SQLiteDatabase::inUse() {
   return m_inUse || m_inTransaction;
}

//******************************************************************************

void SQLiteDatabase::setInUse(bool b) {
   m_inUse = b;
}

//******************************************************************************

bool SQLiteDatabase::inTransaction() {
   return m_inTransaction;
}

//******************************************************************************

void SQLiteDatabase::setInTransaction(bool flag) {
   m_inTransaction = flag;
}

//******************************************************************************

bool SQLiteDatabase::traceExecution() {
   return m_traceExecution;
}

//******************************************************************************

void SQLiteDatabase::setTraceExecution(bool flag) {
   m_traceExecution = flag;
}

//******************************************************************************

bool SQLiteDatabase::checkedOut() {
   return m_checkedOut;
}

//******************************************************************************

void SQLiteDatabase::setCheckedOut(bool flag) {
   m_checkedOut = flag;
}

//******************************************************************************

int SQLiteDatabase::busyRetryTimeout() {
   return m_busyRetryTimeout;
}

//******************************************************************************

void SQLiteDatabase::setBusyRetryTimeout(int busyRetryTimeout) {
   m_busyRetryTimeout = busyRetryTimeout;
}

//******************************************************************************
//******************************************************************************

SQLiteStatement::SQLiteStatement(sqlite3_stmt* value) :
   m_statement(value),
   m_useCount(0L) {   
}

//******************************************************************************

SQLiteStatement::~SQLiteStatement() {
   close();
}

//******************************************************************************

void SQLiteStatement::close() {
   if (m_statement) {
      ::sqlite3_finalize(m_statement);
      m_statement = NULL;
   }
}

//******************************************************************************

void SQLiteStatement::reset() {
   if (m_statement) {
      ::sqlite3_reset(m_statement);
   }
}

//******************************************************************************

sqlite3_stmt* SQLiteStatement::statement() {
   return m_statement;
}

//******************************************************************************

const std::string& SQLiteStatement::query() const {
   return m_query;
}

//******************************************************************************

void SQLiteStatement::setQuery(const std::string& value) {
   m_query = value;
}

//******************************************************************************

long SQLiteStatement::useCount() const {
   return m_useCount;
}

//******************************************************************************

void SQLiteStatement::incrementUseCount() {
   ++m_useCount;
}

//******************************************************************************

std::string SQLiteStatement::description() const {
   char szBuffer[128];
   ::snprintf(szBuffer, 128, "%ld hit(s) for query\n", useCount());
   std::string desc = szBuffer;
   desc += m_query;
   return desc;
}

//******************************************************************************

