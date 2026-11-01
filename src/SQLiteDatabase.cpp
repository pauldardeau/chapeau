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
   m_db(nullptr),
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
   // close() invalidates the live result sets, then empties the cache
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

void SQLiteDatabase::registerCursor(SQLiteResultSet* resultSet) {
   m_cursors.insert(resultSet);
}

//******************************************************************************

void SQLiteDatabase::unregisterCursor(SQLiteResultSet* resultSet) {
   m_cursors.erase(resultSet);
}

//******************************************************************************

SQLiteStatement* SQLiteDatabase::idleCachedStatement(const std::string& sql,
                                                     bool& slotTaken) {
   slotTaken = false;

   SQLiteStatement* statement =
      dynamic_cast<SQLiteStatement*>(cachedStatementForQuery(sql));

   if (statement != nullptr && statement->cursorCount() > 0) {
      // an active result set owns this statement's execution state: running
      // or rebinding it would disturb that result set
      slotTaken = true;
      return nullptr;
   }

   return statement;
}

//******************************************************************************

void SQLiteDatabase::invalidateCursors() {
   // invalidate() must not touch m_cursors, so work on a private copy
   std::set<SQLiteResultSet*> live;
   live.swap(m_cursors);

   for (SQLiteResultSet* resultSet : live) {
      resultSet->invalidate();
   }
}

//******************************************************************************

void SQLiteDatabase::discardCachedStatement(DBStatement* statement) {
   SQLiteStatement* stmt = dynamic_cast<SQLiteStatement*>(statement);

   if (stmt != nullptr && stmt->cursorCount() > 0) {
      // a live result set is still using it: it is no longer the cache's,
      // so the last of those result sets deletes it
      stmt->setCached(false);
   } else {
      Database::discardCachedStatement(statement);
   }
}

//******************************************************************************

void SQLiteDatabase::close() {
   // live result sets give up their statements first, so that every
   // statement can be finalized and the connection actually closes
   invalidateCursors();
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

   m_db = nullptr;
}

//******************************************************************************

bool SQLiteDatabase::goodConnection() {
   if (!m_db) {
      return false;
   }

   DBResultSet* rs =
      executeQuery("select name from sqlite_master where type='table'");

   if (rs != nullptr) {
      rs->close();
      delete rs;
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
                        SQLITE_TRANSIENT);
      if (rc == SQLITE_OK) {
         bindSuccess = true;
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
                        SQLITE_TRANSIENT);
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
   if (nullptr == m_db) {
      ::printf("error: no database connection\n");
      return nullptr;
   }

   if (m_inUse) {
      complainAboutInUse();
      return nullptr;
   }

   MarkInUse inuse(this);

   int rc = 0;
   sqlite3_stmt* pStmt = nullptr;
   SQLiteStatement* statement = nullptr;

   if (m_traceExecution && !sql.empty()) {
      ::printf("executeQuery: %s (%s)\n", sql.c_str(), m_databasePath.c_str());
   }

   const bool cacheStatements = shouldCacheStatements();

   // slotTaken: the cache already holds a statement for this SQL and an active
   // result set owns its execution state, so this execution gets a separate,
   // uncached statement (see idleCachedStatement)
   bool slotTaken = false;

   if (cacheStatements) {
      statement = idleCachedStatement(sql, slotTaken);
      pStmt = statement ? statement->statement() : nullptr;
   }

   const bool cacheThis = cacheStatements && !slotTaken;

   int numberOfRetries = 0;
   bool retry = false;

   if (!pStmt) {
      do {
         retry = false;
         rc = ::sqlite3_prepare_v2(m_db, sql.c_str(), -1, &pStmt, 0);

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
               return nullptr;
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

            return nullptr;
         }
      } while (retry);
   }

   const int queryCount = ::sqlite3_bind_parameter_count(pStmt);

   if (0 != queryCount) {
      Logger::error("the bind count is not correct for the # of variables (executeQuery)");

      // finalize only a statement prepared for this call. One that came from
      // the cache is still the cache's, and stays valid for later executions.
      if (!statement) {
         ::sqlite3_finalize(pStmt);
      }
      return nullptr;
   }

   if (!statement) {
      statement = new SQLiteStatement(pStmt, cacheThis);

      if (cacheThis) {
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
   if (nullptr == m_db) {
      ::printf("error: no database connection\n");
      return nullptr;
   }

   if (m_inUse) {
      complainAboutInUse();
      return nullptr;
   }

   MarkInUse inUse(this);

   int rc = 0;
   sqlite3_stmt* pStmt = nullptr;
   SQLiteStatement* statement = nullptr;

   if (m_traceExecution && !sql.empty()) {
      ::printf("executeQuery: %s (%s)\n",
               sql.c_str(),
               m_databasePath.c_str());
   }

   const bool cacheStatements = shouldCacheStatements();

   // slotTaken: the cache already holds a statement for this SQL and an active
   // result set owns its execution state, so this execution gets a separate,
   // uncached statement (see idleCachedStatement)
   bool slotTaken = false;

   if (cacheStatements) {
      statement = idleCachedStatement(sql, slotTaken);
      pStmt = statement ? statement->statement() : nullptr;
   }

   const bool cacheThis = cacheStatements && !slotTaken;

   int numberOfRetries = 0;
   bool retry = false;

   if (!pStmt) {
      do {
         retry = false;
         rc = ::sqlite3_prepare_v2(m_db, sql.c_str(), -1, &pStmt, 0);

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
               return nullptr;
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

            return nullptr;
         }
      } while (retry);
   }

   const size_t queryCount = ::sqlite3_bind_parameter_count(pStmt);
   const size_t argCount = args.size();

   const bool isNewlyCreatedStatement = !statement;

   if (isNewlyCreatedStatement) {
      statement = new SQLiteStatement(pStmt, cacheThis);
   }

   size_t idx = 0;

   for (; idx < argCount; ++idx) {
      const DBAbstractDataType* arg = args[idx];

      if (arg != nullptr) {
         if (m_traceExecution) {
            ::printf("obj: %s (%s)\n",
                     arg->valueAsString().c_str(),
                     arg->typeName().c_str());
         }

         arg->bind(this, idx+1, statement);
      } else {
         if (m_traceExecution) {
            ::printf("obj: <null>\n");
         }

         bindNull(idx+1, statement);
      }
   }

   if (idx != queryCount) {
      Logger::error("the bind count is not correct for the # of variables (executeQuery)");

      // only tear down a statement created for this call -- one reused
      // from the cache is still valid and may be in use elsewhere, so it
      // must not be finalized/deleted here
      if (isNewlyCreatedStatement) {
         delete statement;
      } else {
         // drop the values bound for the failed call; the statement itself
         // stays valid and idle in the cache
         statement->clearBindings();
      }

      return nullptr;
   }

   // deferred until after the bind-count check succeeds, so a mismatched
   // call never leaves a cache entry pointing at an already-finalized
   // statement
   if (isNewlyCreatedStatement && cacheThis) {
      setCachedStatement(statement, sql);
   }

   statement->incrementUseCount();

   // the statement gets closed in rs dtor or close method
   return new SQLiteResultSet(statement, this, sql);
}

//******************************************************************************

bool SQLiteDatabase::executeUpdate(const std::string& sql,
                                   unsigned long& rowsAffectedCount) {
   if (nullptr == m_db) {
      rowsAffectedCount = 0L;
      ::printf("error: no database connection\n");
      return false;
   }

   if (m_inUse) {
      rowsAffectedCount = 0L;
      complainAboutInUse();
      return false;
   }

   MarkInUse inUse(this);

   int rc = 0;
   sqlite3_stmt* pStmt = nullptr;
   SQLiteStatement* cachedStmt = nullptr;

   if (m_traceExecution && !sql.empty()) {
      ::printf("executeUpdate: %s (%s)\n",
               sql.c_str(),
               m_databasePath.c_str());
   }

   bool cacheStatements = shouldCacheStatements();

   // slotTaken: see executeQuery -- a statement an active result set owns is
   // never reused, a separate uncached one is used instead
   bool slotTaken = false;

   if (cacheStatements) {
      cachedStmt = idleCachedStatement(sql, slotTaken);
      pStmt = cachedStmt ? cachedStmt->statement() : nullptr;
   }

   const bool cacheThis = cacheStatements && !slotTaken;

   int numberOfRetries = 0;
   bool retry = false;

   if (!pStmt) {
      do {
         retry = false;
         rc = ::sqlite3_prepare_v2(m_db, sql.c_str(), -1, &pStmt, 0);
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
               rowsAffectedCount = 0L;
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

            rowsAffectedCount = 0L;

            return false;
         }
      } while (retry);
   }

   const int queryCount = ::sqlite3_bind_parameter_count(pStmt);

   if (0 != queryCount) {
      ::printf("Error: the bind count is not correct for the # of variables (%s) (executeUpdate)\n",
               sql.c_str());

      // finalize only a statement prepared for this call. One that came from
      // the cache is still the cache's, and stays valid for later executions.
      if (!cachedStmt) {
         ::sqlite3_finalize(pStmt);
      }
      rowsAffectedCount = 0L;
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

   if (cacheThis && !cachedStmt) {
      cachedStmt = new SQLiteStatement(pStmt, true);
      setCachedStatement(cachedStmt, sql);
   }

   if (cachedStmt) {
      cachedStmt->incrementUseCount();
      ::sqlite3_reset(pStmt);
   } else {
      /* Finalize the virtual machine. This releases all memory and other
       ** resources allocated by the sqlite3_prepare_v2() call above.
       */
      ::sqlite3_finalize(pStmt);
   }

   bool sqlSuccess = (rc == SQLITE_DONE);

   if (sqlSuccess) {
      rowsAffectedCount = ::sqlite3_changes(m_db);
   } else {
      rowsAffectedCount = 0L;
   }

   return sqlSuccess;
}

//******************************************************************************

bool SQLiteDatabase::executeUpdate(const std::string& sql,
                                   const DBStatementArgs& args,
                                   unsigned long& affectedRowsCount) {
   if (nullptr == m_db) {
      ::printf("error: no database connection\n");
      affectedRowsCount = 0L;
      return false;
   }

   if (m_inUse) {
      complainAboutInUse();
      affectedRowsCount = 0L;
      return false;
   }

   MarkInUse inuse(this);

   int rc = 0;
   sqlite3_stmt* pStmt = nullptr;
   SQLiteStatement* cachedStmt = nullptr;

   const bool cacheStatements = shouldCacheStatements();

   if (m_traceExecution && !sql.empty()) {
      ::printf("executeUpdate: %s (%s)\n",
               sql.c_str(),
               m_databasePath.c_str());
   }

   // slotTaken: see executeQuery -- a statement an active result set owns is
   // never reused, a separate uncached one is used instead
   bool slotTaken = false;

   if (cacheStatements) {
      cachedStmt = idleCachedStatement(sql, slotTaken);
      pStmt = cachedStmt ? cachedStmt->statement() : nullptr;
   }

   const bool cacheThis = cacheStatements && !slotTaken;

   int numberOfRetries = 0;
   bool retry = false;

   if (!pStmt) {
      do {
         retry = false;
         rc = ::sqlite3_prepare_v2(m_db, sql.c_str(), -1, &pStmt, 0);
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
               affectedRowsCount = 0L;
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
            affectedRowsCount = 0L;

            return false;
         }
      } while (retry);
   }

   const size_t queryCount = ::sqlite3_bind_parameter_count(pStmt);
   const size_t argsCount = args.size();

   const bool isNewlyCreatedStatement = !cachedStmt;

   if (isNewlyCreatedStatement) {
      cachedStmt = new SQLiteStatement(pStmt, cacheThis);
   }

   size_t idx = 0;

   for (; idx < argsCount; ++idx) {
      const DBAbstractDataType* arg = args[idx];

      if (arg != nullptr) {
         if (m_traceExecution) {
            ::printf("obj: %s (%s)\n",
                     arg->valueAsString().c_str(),
                     arg->typeName().c_str());
         }

         arg->bind(this, idx+1, cachedStmt);
      } else {
         if (m_traceExecution) {
            ::printf("obj: <null>\n");
         }

         bindNull(idx+1, cachedStmt);
      }
   }

   if (idx != queryCount) {
      ::printf("Error: the bind count is not correct for the # of variables (%s) (executeUpdate)\n",
               sql.c_str());

      // only tear down a statement created for this call -- one reused
      // from the cache is still valid and may be in use elsewhere, so it
      // must not be finalized/deleted here
      if (isNewlyCreatedStatement) {
         delete cachedStmt;
         cachedStmt = nullptr;
      } else {
         // drop the values bound for the failed call; the statement itself
         // stays valid and idle in the cache
         cachedStmt->clearBindings();
      }

      affectedRowsCount = 0L;
      return false;
   }

   // deferred until after the bind-count check succeeds, so a mismatched
   // call never leaves a cache entry pointing at an already-finalized
   // statement
   if (isNewlyCreatedStatement && cacheThis) {
      setCachedStatement(cachedStmt, sql);
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
      ::sqlite3_reset(pStmt);
      if (cacheThis) {
         // nothing keeps a copy of this call's arguments alive in the cache
         cachedStmt->clearBindings();
         cachedStmt->incrementUseCount();
      } else {
         delete cachedStmt;
         cachedStmt = nullptr;
      }
   } else {
      /* Finalize the virtual machine. This releases all memory and other
       ** resources allocated by the sqlite3_prepare_v2() call above.
       */
      ::sqlite3_finalize(pStmt);
   }

   bool sqlSuccess = (rc == SQLITE_DONE);

   if (sqlSuccess) {
      affectedRowsCount = ::sqlite3_changes(m_db);
   } else {
      affectedRowsCount = 0L;
   }

   return sqlSuccess;
}

//******************************************************************************

bool SQLiteDatabase::rollback() {
   unsigned long rowsAffectedCount = 0L;
   const bool b = executeUpdate("ROLLBACK TRANSACTION;", rowsAffectedCount);
   if (b) {
      m_inTransaction = false;
   }

   return b;
}

//******************************************************************************

bool SQLiteDatabase::commit() {
   unsigned long rowsAffectedCount = 0L;
   const bool b = executeUpdate("COMMIT TRANSACTION;", rowsAffectedCount);
   if (b) {
      m_inTransaction = false;
   }

   return b;
}

//******************************************************************************

bool SQLiteDatabase::beginDeferredTransaction() {
   unsigned long rowsAffectedCount = 0L;
   const bool b = executeUpdate("BEGIN DEFERRED TRANSACTION;", rowsAffectedCount);
   if (b) {
      m_inTransaction = true;
   }

   return b;
}

//******************************************************************************

bool SQLiteDatabase::beginTransaction() {
   unsigned long rowsAffectedCount = 0L;
   const bool b = executeUpdate("BEGIN EXCLUSIVE TRANSACTION;", rowsAffectedCount);
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

SQLiteStatement::SQLiteStatement(sqlite3_stmt* value, bool isCached) :
   m_statement(value),
   m_useCount(0L),
   m_isCached(isCached),
   m_cursorCount(0) {
}

//******************************************************************************

SQLiteStatement::~SQLiteStatement() {
   close();
}

//******************************************************************************

void SQLiteStatement::close() {
   if (m_statement) {
      ::sqlite3_finalize(m_statement);
      m_statement = nullptr;
   }
}

//******************************************************************************

void SQLiteStatement::reset() {
   if (m_statement) {
      ::sqlite3_reset(m_statement);
   }
}

//******************************************************************************

void SQLiteStatement::clearBindings() {
   if (m_statement) {
      ::sqlite3_clear_bindings(m_statement);
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

bool SQLiteStatement::isCached() const {
   return m_isCached;
}

//******************************************************************************

void SQLiteStatement::setCached(bool value) {
   m_isCached = value;
}

//******************************************************************************

int SQLiteStatement::cursorCount() const {
   return m_cursorCount;
}

//******************************************************************************

void SQLiteStatement::attachCursor() {
   ++m_cursorCount;
}

//******************************************************************************

int SQLiteStatement::detachCursor() {
   if (m_cursorCount > 0) {
      --m_cursorCount;
   }
   return m_cursorCount;
}

//******************************************************************************

std::string SQLiteStatement::description() const {
   char szBuffer[128];
   ::snprintf(szBuffer, 128, "%ld use(s) for query\n", useCount());
   std::string desc = szBuffer;
   desc += m_query;
   return desc;
}

//******************************************************************************

