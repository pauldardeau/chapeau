//SQLiteDatabase.cpp

#include <stdio.h>

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
#include "UnitState.h"


using namespace chaudiere;
using namespace chapeau;

namespace {

// what the snapshot says when a call is refused as 'in use' (see complainAboutInUse)
const char* const IN_USE_MESSAGE = "the database is in use by an operation that has not finished";

}

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
   m_traceExecution(false),
   m_checkedOut(false),
   m_busyRetryTimeout(DEFAULT_BUSY_TIMEOUT_MS) {
}

//******************************************************************************

SQLiteDatabase::~SQLiteDatabase() {
   // A destructor cannot refuse: a unit in use by another call is a misuse that
   // ends the process with a diagnostic (see UnitClaim::abortUnlessOk).
   UnitClaim claim(unitStatePtr(), "~SQLiteDatabase", UnitClaim::Reporting::Probe);
   claim.abortUnlessOk("~SQLiteDatabase");

   // closing invalidates the live result sets, then empties the cache
   closeInternal();
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
   UnitClaim claim(unitStatePtr(), "open");
   if (!claim.ok()) {
      // from inside a call of this unit on this thread: the in-use case
      if (claim.reentrant()) {
         complainAboutNestedCall();
      }
      return false;
   }

   clearLastError();

   if (m_db != nullptr) {
      // already open: whatever is going on on this connection -- result sets,
      // a transaction, cached statements -- stays exactly as it is
      fail(DBErrorKind::AlreadyOpen, 0,
           "the database is already open; the connection is unchanged",
           "open");
      return false;
   }

   // A SQLite built without threading support (SQLITE_THREADSAFE=0) has no
   // mutexes at all, so none of what Chapeau documents about threads (separate
   // connections on separate threads, a unit handed from one thread to another)
   // holds on it. It is refused here, before a handle is created or a file is
   // touched, and the object stays as it was: not open, and able to try again.
   // This reads the library's compile-time mode only; see the class comment.
   if (0 == ::sqlite3_threadsafe()) {
      fail(DBErrorKind::UnsupportedLibrary, 0,
           "unsupported library: SQLite " + std::string(::sqlite3_libversion()) +
           " was compiled without threading support (sqlite3_threadsafe() returned 0), "
           "which Chapeau requires; nothing was opened. Use a SQLite build with "
           "threading enabled (SQLITE_THREADSAFE=1 or 2)",
           "open", m_databasePath);
      return false;
   }

   Logger::debug("opening database '" + m_databasePath + "'");
   sqlite3* handle = nullptr;
   const int err = ::sqlite3_open(m_databasePath.c_str(), &handle);
   if (err != SQLITE_OK) {
      // sqlite3_open hands back a handle even when it fails, and that handle
      // has to be closed; the reason is taken from it first
      const int code = (handle != nullptr) ? ::sqlite3_errcode(handle) : err;
      const std::string message = (handle != nullptr) ? ::sqlite3_errmsg(handle)
                                                      : ::sqlite3_errstr(err);
      ::sqlite3_close(handle);

      fail(DBErrorKind::Sqlite, code, message, "open", m_databasePath);
      return false;
   }

   m_db = handle;

   // the timeout set so far (the default, or what the caller chose before
   // opening) takes effect on the new connection
   ::sqlite3_busy_timeout(m_db, m_busyRetryTimeout);

   return true;
}

//******************************************************************************

bool SQLiteDatabase::prepareStatement(const std::string& sql,
                                      sqlite3_stmt** ppStmt,
                                      const char* operation) {
   *ppStmt = nullptr;

   // SQLite reads the text up to its first NUL byte and stops there, so the
   // rest would be dropped without a word -- including statements after it.
   // Checked before anything is prepared.
   const std::string::size_type nulOffset = sql.find('\0');

   if (nulOffset != std::string::npos) {
      std::string shown;
      for (const char c : sql) {
         if ('\0' == c) {
            shown += "\\0";
         } else {
            shown += c;
         }
      }

      fail(DBErrorKind::RefusedStatement, 0,
           "the SQL contains a NUL byte at offset " + std::to_string(nulOffset) +
           "; nothing was run",
           operation,
           shown);
      return false;
   }

   // a prepare that has to read the schema needs a lock, and waits for it as
   // long as the busy timeout allows
   const char* tail = nullptr;
   const int rc = ::sqlite3_prepare_v2(m_db, sql.c_str(), -1, ppStmt, &tail);

   if (SQLITE_OK == rc) {
      return !refuseTrailingSql(sql, tail, ppStmt, operation);
   }

   // SQLite's explanation is taken before anything else is called on the
   // connection
   const std::string message = ::sqlite3_errmsg(m_db);

   ::sqlite3_finalize(*ppStmt);
   *ppStmt = nullptr;

   fail(DBErrorKind::Sqlite, rc, message, operation, sql);

   return false;
}

//******************************************************************************

bool SQLiteDatabase::refuseTrailingSql(const std::string& sql,
                                       const char* tail,
                                       sqlite3_stmt** ppStmt,
                                       const char* operation) {
   if (nullptr == tail || '\0' == *tail) {
      return false;
   }

   // What is left after the first statement is prepared to tell what it is,
   // not to be run. Whitespace, semicolons and comments prepare to no
   // statement at all, and that is all that may follow. Anything else is a
   // statement or an error, however it comes out; a statement that depends on
   // the first one (it uses a table the first creates) cannot be prepared yet,
   // and is refused all the same. No parser is needed.
   sqlite3_stmt* more = nullptr;
   const int rc = ::sqlite3_prepare_v2(m_db, tail, -1, &more, nullptr);

   if (SQLITE_OK == rc && nullptr == more) {
      return false;
   }

   std::string reason;

   if (SQLITE_OK != rc) {
      reason = std::string("; the trailing SQL does not prepare: ") +
               ::sqlite3_errmsg(m_db);
   }

   ::sqlite3_finalize(more);

   // the first statement is let go of without having been run
   ::sqlite3_finalize(*ppStmt);
   *ppStmt = nullptr;

   // the offset is that of the first byte of the trailing SQL that is not
   // whitespace
   const char* start = tail;
   while (' ' == *start || '\t' == *start || '\n' == *start ||
          '\r' == *start || '\f' == *start || '\v' == *start) {
      ++start;
   }

   fail(DBErrorKind::RefusedStatement, 0,
        "the SQL has more after its first statement, and only one statement "
        "is allowed per call; nothing was run (the trailing SQL starts at "
        "offset " + std::to_string(start - sql.c_str()) + ")" + reason,
        operation,
        sql);

   return true;
}

//******************************************************************************

DBResultSet* SQLiteDatabase::openResultSet(SQLiteStatement* statement,
                                           const std::string& sql) {
   // the statement gets closed in rs dtor or close method
   statement->incrementUseCount();
   SQLiteResultSet* resultSet = new SQLiteResultSet(statement, this, sql);

   // A statement with result columns is run by stepping the result set. One
   // without -- INSERT, UPDATE, DELETE, DDL, BEGIN, a PRAGMA that sets a
   // value -- has no rows to step to, so nothing would ever run it: it is run
   // here, to completion, through the same code that next() uses. The result
   // set that comes back is then already Done, or Error with the failure in
   // it, and has let go of its statement.
   if (::sqlite3_column_count(statement->statement()) == 0) {
      resultSet->advance("executeQuery");

      if (DBResultSetStatus::Done == resultSet->status()) {
         // this call succeeded, whatever a re-entrant call made from inside it
         // (an SQL function that used this database) recorded meanwhile
         clearLastError();
      }
   }

   return resultSet;
}

//******************************************************************************

bool SQLiteDatabase::refuseRowReturningUpdate(sqlite3_stmt* pStmt,
                                              const std::string& sql,
                                              bool ownedByCache) {
   // A statement with result columns can return rows: a SELECT, a PRAGMA that
   // reads a value, INSERT ... RETURNING, EXPLAIN, VALUES. (sqlite3_stmt_readonly
   // is no help: INSERT ... RETURNING is not read-only, and BEGIN is.) It is
   // refused before it is run, because running it is not harmless -- the first
   // step performs an INSERT ... RETURNING or a PRAGMA journal_mode=... and
   // calls whatever functions a SELECT uses -- so that a false return from
   // executeUpdate() always means that nothing was done.
   if (::sqlite3_column_count(pStmt) == 0) {
      return false;
   }

   fail(DBErrorKind::RefusedStatement, 0,
        "the statement returns rows and was not run; use executeQuery",
        "executeUpdate",
        sql);

   // finalize only a statement prepared for this call. One that came from
   // the cache is still the cache's, and stays valid for later executions.
   if (!ownedByCache) {
      ::sqlite3_finalize(pStmt);
   }

   return true;
}

//******************************************************************************

void SQLiteDatabase::fail(DBErrorKind kind,
                          int code,
                          const std::string& message,
                          const char* operation,
                          const std::string& detail) {
   // the record comes first and does not depend on the logging switch
   setLastError(kind, code, message);

   if (!m_logsErrors) {
      return;
   }

   std::string line = std::string("DB Error [") + dbErrorKindName(kind) + "]";

   if (code != 0) {
      line += " code=" + std::to_string(code);
   }

   line += ": " + message;

   if (DBErrorKind::Sqlite == kind && SQLITE_BUSY == code) {
      line += " (gave up after waiting " +
              std::to_string(m_busyRetryTimeout) + " ms)";
   }

   line += std::string(" in ") + operation;

   if (!detail.empty()) {
      line += ": " + detail;
   }

   line += " (database: " + m_databasePath + ")";

   ::printf("%s\n", line.c_str());
}

//******************************************************************************

std::string SQLiteDatabase::stepFailed(const char* operation,
                                       int rc,
                                       const std::string& sql) {
   // SQLite's explanation of this very failure, before the statement is reset
   const std::string message = (m_db != nullptr) ? ::sqlite3_errmsg(m_db)
                                                 : ::sqlite3_errstr(rc);
   fail(DBErrorKind::Sqlite, rc, message, operation, sql);
   return message;
}

//******************************************************************************

void SQLiteDatabase::bindCountFailed(const char* operation,
                                     const std::string& sql,
                                     size_t parameterCount,
                                     size_t argumentCount) {
   fail(DBErrorKind::BindCount, 0,
        "the statement has " + std::to_string(parameterCount) +
        " parameter(s) but " + std::to_string(argumentCount) +
        " argument(s) were given",
        operation,
        sql);
}

//******************************************************************************

bool SQLiteDatabase::bindArguments(const char* operation,
                                   const std::string& sql,
                                   const DBStatementArgs& args,
                                   SQLiteStatement* statement) {
   const size_t argCount = args.size();

   for (size_t idx = 0; idx < argCount; ++idx) {
      const int parameter = static_cast<int>(idx) + 1;
      const DBAbstractDataType* arg = args[idx];
      bool bound = false;

      if (arg != nullptr) {
         if (m_traceExecution) {
            ::printf("obj: %s (%s)\n",
                     arg->valueAsString().c_str(),
                     arg->typeName().c_str());
         }

         bound = arg->bind(this, parameter, statement);
      } else {
         if (m_traceExecution) {
            ::printf("obj: <null>\n");
         }

         bound = bindNull(parameter, statement);
      }

      if (!bound) {
         // a bind function that was refused by SQLite has recorded SQLite's
         // code and message; one that failed for another reason has not, and
         // then there is no code to give
         int code = 0;
         std::string reason = "the argument could not be bound";

         if (DBErrorKind::BindFailed == lastErrorKind()) {
            code = lastErrorCode();
            reason = lastErrorMessage();
         }

         fail(DBErrorKind::BindFailed, code,
              "parameter " + std::to_string(parameter) + ": " + reason,
              operation,
              sql);
         return false;
      }
   }

   return true;
}

//******************************************************************************

bool SQLiteDatabase::bindResult(int rc) {
   if (SQLITE_OK == rc) {
      return true;
   }

   // SQLite sets the connection's error for most failures of a bind call; when
   // it did not, the text for the code stands in for it
   const std::string message =
      (m_db != nullptr && ::sqlite3_errcode(m_db) == rc) ? ::sqlite3_errmsg(m_db)
                                                         : ::sqlite3_errstr(rc);
   setLastError(DBErrorKind::BindFailed, rc, message);
   return false;
}

//******************************************************************************

bool SQLiteDatabase::noStatementToBind() {
   setLastError(DBErrorKind::BindFailed, 0, "there is no statement to bind to");
   return false;
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
      dynamic_cast<SQLiteStatement*>(cachedStatementForQueryInternal(sql));

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
   UnitClaim claim(unitStatePtr(), "close");
   if (!claim.ok()) {
      // from inside a call of this unit on this thread: the in-use case
      if (claim.reentrant()) {
         complainAboutNestedCall();
      }
      return;
   }

   closeInternal();
}

//******************************************************************************

void SQLiteDatabase::closeInternal() {
   clearLastError();

   // live result sets give up their statements first, so that every
   // statement can be finalized and the connection actually closes
   invalidateCursors();
   clearCachedStatementsInternal();

   if (!m_db) {
      return;
   }

   // sqlite3_close() does not wait for locks held by other connections; it
   // is refused (SQLITE_BUSY) only while a statement is still unfinalized,
   // and every statement of ours was finalized above. Should one be left,
   // sqlite3_close_v2() lets the connection go anyway and frees it as soon as
   // that statement is finalized, instead of waiting for something that never
   // happens.
   int rc = ::sqlite3_close(m_db);
   if (SQLITE_BUSY == rc) {
      Logger::warning("Database busy, closing with unfinalized statements");
      rc = ::sqlite3_close_v2(m_db);
   }
   const std::string closeMessage =
      (SQLITE_OK != rc) ? ::sqlite3_errstr(rc) : std::string();

   m_db = nullptr;

   if (SQLITE_OK != rc) {
      fail(DBErrorKind::Sqlite, rc, closeMessage, "close", m_databasePath);
   }
}

//******************************************************************************

bool SQLiteDatabase::goodConnection() {
   DBResultSet* rs = nullptr;

   {
      UnitClaim claim(unitStatePtr(), "goodConnection");
      if (!claim.ok()) {
         // a call made from inside a call of this unit, on its own thread, is the
         // in-use case; any other refusal says nothing to the snapshot
         if (claim.reentrant()) {
            complainAboutNestedCall();
         }
         return false;
      }

      if (!m_db) {
         fail(DBErrorKind::NoConnection, 0, "there is no database connection",
              "goodConnection");
         return false;
      }

      // executeQuery() starts by clearing the error and records its own failure,
      // which is then this one. The query is the whole of the database's part of
      // this call; the result set is ended while the claim is held, and let go of
      // after it as a separate step (its destructor takes the claim itself).
      rs = executeQueryInternal("select name from sqlite_master where type='table'");

      if (rs != nullptr) {
         static_cast<SQLiteResultSet*>(rs)->closeInternal();
      }
   }

   if (rs != nullptr) {
      delete rs;
      return true;
   }

   return false;
}

//******************************************************************************

void SQLiteDatabase::complainAboutInUse() {
   fail(DBErrorKind::InUse, 0,
        IN_USE_MESSAGE,
        "use of the database");

   if (m_crashOnErrors) {
   }
}

//******************************************************************************

void SQLiteDatabase::complainAboutNestedCall() {
   if (!m_nestedRefusal.noted) {
      // the first one of this call: keep what is there, so that it can be put back
      m_nestedRefusal.before = lastErrorRecord();
      m_nestedRefusal.noted = true;
   }

   complainAboutInUse();
}

//******************************************************************************

void SQLiteDatabase::settleNestedRefusal() {
   m_nestedRefusal.noted = false;

   // Put the snapshot back only if what is on it is still the InUse of a refused
   // call. If the outer call wrote its own result since (a failure, or the
   // clearing of the snapshot that a success does), that result is the one to keep.
   const LastError now = lastErrorRecord();
   if (now.kind == DBErrorKind::InUse && now.code == 0 && now.message == IN_USE_MESSAGE) {
      setLastError(m_nestedRefusal.before.kind,
                   m_nestedRefusal.before.code,
                   m_nestedRefusal.before.message);
   }
}

//******************************************************************************

bool SQLiteDatabase::hadError() const {
   return (lastErrorKind() != DBErrorKind::None);
}

//******************************************************************************

long long int SQLiteDatabase::lastInsertRowId() {
   UnitClaim claim(unitStatePtr(), "lastInsertRowId");
   if (!claim.ok()) {
      // from inside a call of this unit on this thread: the in-use case
      if (claim.reentrant()) {
         complainAboutNestedCall();
      }
      return -1;
   }

   if (nullptr == m_db) {
      fail(DBErrorKind::NoConnection, 0, "there is no database connection",
           "lastInsertRowId");
      return -1;
   }

   if (m_inUse) {
      complainAboutInUse();
      return -1;
   }

   MarkInUse inuse(this);
   return ::sqlite3_last_insert_rowid(m_db);
}

//******************************************************************************

bool SQLiteDatabase::bindNull(int idx, DBStatement* statement) {
   SQLiteStatement* stmt = dynamic_cast<SQLiteStatement*>(statement);
   if (!stmt || !stmt->statement()) {
      return noStatementToBind();
   }

   return bindResult(::sqlite3_bind_null(stmt->statement(), idx));
}

//******************************************************************************

bool SQLiteDatabase::bindData(const DBData* data, int idx, DBStatement* statement) {
   SQLiteStatement* stmt = dynamic_cast<SQLiteStatement*>(statement);
   if (!stmt || !stmt->statement()) {
      return noStatementToBind();
   }

   // SQLite binds NULL when the pointer it is given is null, whatever the
   // length, and an empty DBData has no buffer to point to. A zero-length
   // BLOB is asked for explicitly, so that it is not stored as NULL; NULL
   // itself is bound by bindNull() (DBNull, or a null argument).
   const int rc = (data->length() == 0)
      ? ::sqlite3_bind_zeroblob(stmt->statement(), idx, 0)
      : ::sqlite3_bind_blob(stmt->statement(),
                            idx,
                            data->value(),
                            (int) data->length(),
                            SQLITE_TRANSIENT);
   return bindResult(rc);
}

//******************************************************************************

bool SQLiteDatabase::bindDate(const DBDate* value, int idx, DBStatement* statement) {
   SQLiteStatement* stmt = dynamic_cast<SQLiteStatement*>(statement);
   if (!stmt || !stmt->statement()) {
      return noStatementToBind();
   }

   return bindResult(::sqlite3_bind_double(stmt->statement(),
                                           idx,
                                           value->timeIntervalSince1970()));
}

//******************************************************************************

bool SQLiteDatabase::bindBool(bool value, int idx, DBStatement* statement) {
   SQLiteStatement* stmt = dynamic_cast<SQLiteStatement*>(statement);
   if (!stmt || !stmt->statement()) {
      return noStatementToBind();
   }

   return bindResult(::sqlite3_bind_int(stmt->statement(), idx, (value ? 1 : 0)));
}

//******************************************************************************

bool SQLiteDatabase::bindInt(int value, int idx, DBStatement* statement) {
   SQLiteStatement* stmt = dynamic_cast<SQLiteStatement*>(statement);
   if (!stmt || !stmt->statement()) {
      return noStatementToBind();
   }

   return bindResult(::sqlite3_bind_int64(stmt->statement(), idx, value));
}

//******************************************************************************

bool SQLiteDatabase::bindLong(long value, int idx, DBStatement* statement) {
   SQLiteStatement* stmt = dynamic_cast<SQLiteStatement*>(statement);
   if (!stmt || !stmt->statement()) {
      return noStatementToBind();
   }

   return bindResult(::sqlite3_bind_int64(stmt->statement(), idx, value));
}

//******************************************************************************

bool SQLiteDatabase::bindFloat(float value, int idx, DBStatement* statement) {
   SQLiteStatement* stmt = dynamic_cast<SQLiteStatement*>(statement);
   if (!stmt || !stmt->statement()) {
      return noStatementToBind();
   }

   return bindResult(::sqlite3_bind_double(stmt->statement(), idx, value));
}

//******************************************************************************

bool SQLiteDatabase::bindDouble(double value, int idx, DBStatement* statement) {
   SQLiteStatement* stmt = dynamic_cast<SQLiteStatement*>(statement);
   if (!stmt || !stmt->statement()) {
      return noStatementToBind();
   }

   return bindResult(::sqlite3_bind_double(stmt->statement(), idx, value));
}

//******************************************************************************

bool SQLiteDatabase::bindString(const std::string& value,
                            int idx,
                            DBStatement* statement) {
   SQLiteStatement* stmt = dynamic_cast<SQLiteStatement*>(statement);
   if (!stmt || !stmt->statement()) {
      return noStatementToBind();
   }

   return bindResult(::sqlite3_bind_text(stmt->statement(),
                                         idx,
                                         value.c_str(),
                                         value.length(),
                                         SQLITE_TRANSIENT));
}

//******************************************************************************

DBResultSet* SQLiteDatabase::executeQuery(const std::string& sql) {
   UnitClaim claim(unitStatePtr(), "executeQuery");
   if (!claim.ok()) {
      // from inside a call of this unit on this thread: the in-use case
      if (claim.reentrant()) {
         complainAboutNestedCall();
      }
      return nullptr;
   }

   return executeQueryInternal(sql);
}

//******************************************************************************

DBResultSet* SQLiteDatabase::executeQueryInternal(const std::string& sql) {
   NestedRefusalScope nestedRefusals(this);
   clearLastError();

   if (nullptr == m_db) {
      fail(DBErrorKind::NoConnection, 0, "there is no database connection",
           "executeQuery", sql);
      return nullptr;
   }

   if (m_inUse) {
      complainAboutInUse();
      return nullptr;
   }

   MarkInUse inuse(this);

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

   if (!pStmt) {
      if (!prepareStatement(sql, &pStmt, "executeQuery")) {
         return nullptr;
      }
   }

   const int queryCount = ::sqlite3_bind_parameter_count(pStmt);

   if (0 != queryCount) {
      // finalize only a statement prepared for this call. One that came from
      // the cache is still the cache's, and stays valid for later executions
      // (nothing was bound to it).
      if (!statement) {
         ::sqlite3_finalize(pStmt);
      }

      bindCountFailed("executeQuery", sql, queryCount, 0);
      return nullptr;
   }

   if (!statement) {
      statement = new SQLiteStatement(pStmt, cacheThis);

      if (cacheThis) {
         setCachedStatement(statement,sql);
      }
   }

   return openResultSet(statement, sql);
}

//******************************************************************************

DBResultSet* SQLiteDatabase::executeQuery(const std::string& sql,
                                          const DBStatementArgs& args) {
   UnitClaim claim(unitStatePtr(), "executeQuery");
   if (!claim.ok()) {
      // from inside a call of this unit on this thread: the in-use case
      if (claim.reentrant()) {
         complainAboutNestedCall();
      }
      return nullptr;
   }

   return executeQueryInternal(sql, args);
}

//******************************************************************************

DBResultSet* SQLiteDatabase::executeQueryInternal(const std::string& sql,
                                                  const DBStatementArgs& args) {
   NestedRefusalScope nestedRefusals(this);
   clearLastError();

   if (nullptr == m_db) {
      fail(DBErrorKind::NoConnection, 0, "there is no database connection",
           "executeQuery", sql);
      return nullptr;
   }

   if (m_inUse) {
      complainAboutInUse();
      return nullptr;
   }

   MarkInUse inUse(this);

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

   if (!pStmt) {
      if (!prepareStatement(sql, &pStmt, "executeQuery")) {
         return nullptr;
      }
   }

   const size_t queryCount = ::sqlite3_bind_parameter_count(pStmt);
   const size_t argCount = args.size();

   const bool isNewlyCreatedStatement = !statement;

   if (argCount != queryCount) {
      // counted before anything is bound, so that no surplus argument is bound
      // to a parameter that does not exist. A statement from the cache is still
      // valid and may be in use elsewhere, so only one prepared for this call
      // is finalized.
      if (isNewlyCreatedStatement) {
         ::sqlite3_finalize(pStmt);
      }

      bindCountFailed("executeQuery", sql, queryCount, argCount);
      return nullptr;
   }

   if (isNewlyCreatedStatement) {
      statement = new SQLiteStatement(pStmt, cacheThis);
   }

   if (!bindArguments("executeQuery", sql, args, statement)) {
      // the statement is not run. Only one created for this call is torn down
      // -- one reused from the cache is still valid and may be in use
      // elsewhere, so it must not be finalized/deleted here
      if (isNewlyCreatedStatement) {
         delete statement;
      } else {
         // drop the values bound before the failure; the statement itself
         // stays valid and idle in the cache
         statement->clearBindings();
      }

      return nullptr;
   }

   // deferred until after the bind checks succeed, so a failed call never
   // leaves a cache entry pointing at an already-finalized statement
   if (isNewlyCreatedStatement && cacheThis) {
      setCachedStatement(statement, sql);
   }

   return openResultSet(statement, sql);
}

//******************************************************************************

bool SQLiteDatabase::executeUpdate(const std::string& sql,
                                   unsigned long& rowsAffectedCount) {
   UnitClaim claim(unitStatePtr(), "executeUpdate");
   if (!claim.ok()) {
      rowsAffectedCount = 0L;
      // from inside a call of this unit on this thread: the in-use case
      if (claim.reentrant()) {
         complainAboutNestedCall();
      }
      return false;
   }

   return executeUpdateInternal(sql, rowsAffectedCount);
}

//******************************************************************************

bool SQLiteDatabase::executeUpdateInternal(const std::string& sql,
                                           unsigned long& rowsAffectedCount) {
   clearLastError();

   if (nullptr == m_db) {
      rowsAffectedCount = 0L;
      fail(DBErrorKind::NoConnection, 0, "there is no database connection",
           "executeUpdate", sql);
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

   if (!pStmt) {
      if (!prepareStatement(sql, &pStmt, "executeUpdate")) {
         rowsAffectedCount = 0L;
         return false;
      }
   }

   if (refuseRowReturningUpdate(pStmt, sql, cachedStmt != nullptr)) {
      rowsAffectedCount = 0L;
      return false;
   }

   const int queryCount = ::sqlite3_bind_parameter_count(pStmt);

   if (0 != queryCount) {
      // finalize only a statement prepared for this call. One that came from
      // the cache is still the cache's, and stays valid for later executions
      // (nothing was bound to it).
      if (!cachedStmt) {
         ::sqlite3_finalize(pStmt);
      }

      bindCountFailed("executeUpdate", sql, queryCount, 0);
      rowsAffectedCount = 0L;
      return false;
   }

   /* Call sqlite3_step() to run the virtual machine. Since the SQL being
    ** executed is not a SELECT statement, we assume no data will be returned.
    */
   // sqlite3_step() waits for a lock for as long as the busy timeout allows
   // (see setBusyRetryTimeout) and then fails with SQLITE_BUSY like any
   // other error
   rc = ::sqlite3_step(pStmt);

   if (SQLITE_DONE != rc) {
      // recorded before the statement is reset or finalized below. (A
      // statement without result columns, which is all that gets here, cannot
      // return a row; if SQLite ever did, it would fail like any other.)
      stepFailed("executeUpdate", rc, sql);
   }

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
      // the result of this call is its own: whatever a re-entrant call made
      // from inside it (an SQL function that used this database) recorded
      // meanwhile does not stay. (executeQuery() runs no SQL, so no function
      // of the caller's can run inside it.)
      clearLastError();
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
   UnitClaim claim(unitStatePtr(), "executeUpdate");
   if (!claim.ok()) {
      affectedRowsCount = 0L;
      // from inside a call of this unit on this thread: the in-use case
      if (claim.reentrant()) {
         complainAboutNestedCall();
      }
      return false;
   }

   return executeUpdateInternal(sql, args, affectedRowsCount);
}

//******************************************************************************

bool SQLiteDatabase::executeUpdateInternal(const std::string& sql,
                                           const DBStatementArgs& args,
                                           unsigned long& affectedRowsCount) {
   clearLastError();

   if (nullptr == m_db) {
      fail(DBErrorKind::NoConnection, 0, "there is no database connection",
           "executeUpdate", sql);
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

   if (!pStmt) {
      if (!prepareStatement(sql, &pStmt, "executeUpdate")) {
         affectedRowsCount = 0L;
         return false;
      }
   }

   if (refuseRowReturningUpdate(pStmt, sql, cachedStmt != nullptr)) {
      affectedRowsCount = 0L;
      return false;
   }

   const size_t queryCount = ::sqlite3_bind_parameter_count(pStmt);
   const size_t argsCount = args.size();

   const bool isNewlyCreatedStatement = !cachedStmt;

   if (argsCount != queryCount) {
      // counted before anything is bound, so that no surplus argument is bound
      // to a parameter that does not exist. A statement from the cache is still
      // valid and may be in use elsewhere, so only one prepared for this call
      // is finalized.
      if (isNewlyCreatedStatement) {
         ::sqlite3_finalize(pStmt);
      }

      bindCountFailed("executeUpdate", sql, queryCount, argsCount);
      affectedRowsCount = 0L;
      return false;
   }

   if (isNewlyCreatedStatement) {
      cachedStmt = new SQLiteStatement(pStmt, cacheThis);
   }

   if (!bindArguments("executeUpdate", sql, args, cachedStmt)) {
      // the statement is not run. Only one created for this call is torn down
      // -- one reused from the cache is still valid and may be in use
      // elsewhere, so it must not be finalized/deleted here
      if (isNewlyCreatedStatement) {
         delete cachedStmt;
         cachedStmt = nullptr;
      } else {
         // drop the values bound before the failure; the statement itself
         // stays valid and idle in the cache
         cachedStmt->clearBindings();
      }

      affectedRowsCount = 0L;
      return false;
   }

   // deferred until after the bind checks succeed, so a failed call never
   // leaves a cache entry pointing at an already-finalized statement
   if (isNewlyCreatedStatement && cacheThis) {
      setCachedStatement(cachedStmt, sql);
   }

   /* Call sqlite3_step() to run the virtual machine. Since the SQL being
    ** executed is not a SELECT statement, we assume no data will be returned.
    */
   // sqlite3_step() waits for a lock for as long as the busy timeout allows
   // (see setBusyRetryTimeout) and then fails with SQLITE_BUSY like any
   // other error
   rc = ::sqlite3_step(pStmt);

   if (SQLITE_DONE != rc) {
      // recorded before the statement is reset or finalized below. (A
      // statement without result columns, which is all that gets here, cannot
      // return a row; if SQLite ever did, it would fail like any other.)
      stepFailed("executeUpdate", rc, sql);
   }

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
      // the result of this call is its own: whatever a re-entrant call made
      // from inside it (an SQL function that used this database) recorded
      // meanwhile does not stay. (executeQuery() runs no SQL, so no function
      // of the caller's can run inside it.)
      clearLastError();
      affectedRowsCount = ::sqlite3_changes(m_db);
   } else {
      affectedRowsCount = 0L;
   }

   return sqlSuccess;
}

//******************************************************************************

bool SQLiteDatabase::rollback() {
   UnitClaim claim(unitStatePtr(), "rollback");
   if (!claim.ok()) {
      // from inside a call of this unit on this thread: the in-use case
      if (claim.reentrant()) {
         complainAboutNestedCall();
      }
      return false;
   }

   unsigned long rowsAffectedCount = 0L;
   // whether a transaction is open afterwards is SQLite's to say, see
   // inTransaction()
   return executeUpdateInternal("ROLLBACK TRANSACTION;", rowsAffectedCount);
}

//******************************************************************************

bool SQLiteDatabase::commit() {
   UnitClaim claim(unitStatePtr(), "commit");
   if (!claim.ok()) {
      // from inside a call of this unit on this thread: the in-use case
      if (claim.reentrant()) {
         complainAboutNestedCall();
      }
      return false;
   }

   unsigned long rowsAffectedCount = 0L;
   // whether a transaction is open afterwards is SQLite's to say, see
   // inTransaction()
   return executeUpdateInternal("COMMIT TRANSACTION;", rowsAffectedCount);
}

//******************************************************************************

bool SQLiteDatabase::beginDeferredTransaction() {
   UnitClaim claim(unitStatePtr(), "beginDeferredTransaction");
   if (!claim.ok()) {
      // from inside a call of this unit on this thread: the in-use case
      if (claim.reentrant()) {
         complainAboutNestedCall();
      }
      return false;
   }

   unsigned long rowsAffectedCount = 0L;
   return executeUpdateInternal("BEGIN DEFERRED TRANSACTION;", rowsAffectedCount);
}

//******************************************************************************

bool SQLiteDatabase::beginTransaction() {
   UnitClaim claim(unitStatePtr(), "beginTransaction");
   if (!claim.ok()) {
      // from inside a call of this unit on this thread: the in-use case
      if (claim.reentrant()) {
         complainAboutNestedCall();
      }
      return false;
   }

   unsigned long rowsAffectedCount = 0L;
   return executeUpdateInternal("BEGIN EXCLUSIVE TRANSACTION;", rowsAffectedCount);
}

//******************************************************************************

bool SQLiteDatabase::logsErrors() {
   return m_logsErrors;
}

//******************************************************************************

void SQLiteDatabase::setLogsErrors(bool flag) {
   m_logsErrors = flag;
   // the unit state is told too, so that a refusal made by a result set can be
   // logged after this database is gone
   unitState()->setLoggingEnabled(flag);
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
   // A probe: "is the unit busy?" is what a pool asks, so a busy unit is the
   // answer (true) and not a violation: nothing is counted, logged or recorded,
   // and SQLite is not looked at without the claim. When the claim is ours the
   // answer is what it always was.
   UnitClaim probe(unitStatePtr(), "inUse", UnitClaim::Reporting::Probe);
   if (!probe.ok()) {
      return true;
   }

   return m_inUse || inTransactionInternal();
}

//******************************************************************************

void SQLiteDatabase::setInUse(bool b) {
   m_inUse = b;
}

//******************************************************************************

bool SQLiteDatabase::inTransaction() {
   UnitClaim claim(unitStatePtr(), "inTransaction");
   if (!claim.ok()) {
      // from inside a call of this unit on this thread: the in-use case
      if (claim.reentrant()) {
         complainAboutNestedCall();
      }
      return false;
   }

   return inTransactionInternal();
}

//******************************************************************************

bool SQLiteDatabase::inTransactionInternal() {
   // SQLite knows: it is in autocommit mode exactly when no transaction is
   // open, however the transaction was begun or ended (a BEGIN or COMMIT run
   // as plain SQL, a COMMIT that failed busy and left it open, or a
   // statement that rolled it back). No copy of that is kept here that could
   // go stale.
   return (m_db != nullptr) && (::sqlite3_get_autocommit(m_db) == 0);
}

//******************************************************************************

void SQLiteDatabase::setInTransaction(bool flag) {
   // kept so that existing callers still build; the state of the transaction
   // is SQLite's, and cannot be set from outside
   (void) flag;
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
   UnitClaim claim(unitStatePtr(), "setBusyRetryTimeout");
   if (!claim.ok()) {
      return;
   }

   m_busyRetryTimeout = (busyRetryTimeout > 0) ? busyRetryTimeout : 0;

   // on an open connection the new value takes effect at once; otherwise
   // open() applies it
   if (m_db != nullptr) {
      ::sqlite3_busy_timeout(m_db, m_busyRetryTimeout);
   }
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

