#ifndef CHAPEAU_SQLITEDATABASE_H
#define CHAPEAU_SQLITEDATABASE_H

#include <string>
#include <map>
#include <set>
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

/**
 * A SQLite connection, with its statement cache.
 *
 * Concurrency: not thread safe. The database, its result sets and its
 * statements are one unit, used by one thread at a time and moved between
 * threads only while idle, with synchronization the application provides.
 * Different SQLiteDatabase objects may be used by different threads at once.
 * The full rules are in the comment of Database.
 *
 * What this class adds to them:
 *  - sqliteHandle() is part of the unit. Using the handle while another thread
 *    uses the database is a violation like any other.
 *  - inUse() / the in-use marker is not a lock. It is a plain bool, set while
 *    one of this database's calls runs and cleared when it returns or when the
 *    result set it covers ends; it only catches a re-entrant call on the same
 *    thread (DBErrorKind::InUse). Another thread cannot rely on seeing it, and
 *    it is not atomic.
 *  - SQLite's serialized mode makes the sqlite3* connection safe to call from
 *    several threads. It does not protect the statement cache, the set of live
 *    result sets, the error snapshot or the cursor counts kept here.
 *  - close() and the destructor invalidate every live result set, so neither
 *    may overlap any use of those result sets.
 *  - open() does not check sqlite3_threadsafe(); a SQLite built without thread
 *    safety must not be used from more than one thread at all.
 *  - Busy handling (setBusyRetryTimeout()) is per connection, and is how
 *    connections to one file wait for each other.
 */
class SQLiteDatabase : public Database {

public:
   SQLiteDatabase(const std::string& inPath);
   ~SQLiteDatabase();

   /**
    * Opens the database file. Fails (returns false) if this object already has
    * an open connection (DBErrorKind::AlreadyOpen), and then changes nothing:
    * the connection, its result sets, its transaction and its cached
    * statements stay as they are. It also fails if the file cannot be opened
    * (DBErrorKind::Sqlite), and then no connection is left behind, the reason
    * is available from lastErrorCode() and lastErrorMessage(), and open() may
    * be tried again (for instance once the missing directory exists).
    */
   bool open();

   /**
    * Closes the connection: its result sets are invalidated, every statement
    * is finalized, an open transaction is rolled back. Safe on a database that
    * is not open, and safe to repeat.
    */
   void close();
   bool goodConnection();

   // The bind functions return false when the value could not be bound (SQLite
   // refused it, or there is no statement), and then record DBErrorKind::BindFailed
   // with SQLite's code and message as the last error (code 0 if SQLite was not
   // involved). They do not log, and a success does not clear the error: they are
   // the steps of executeQuery() and executeUpdate(), which check every one of
   // them, do not run the statement when one fails, and report which parameter.
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

   // The last error (lastErrorKind(), lastErrorCode() and lastErrorMessage())
   // is Database's snapshot, see there. Here, a failure of SQLite itself
   // (prepare, step, open, close, a failed bind) carries SQLite's result code and
   // sqlite3_errmsg() text as they were when it happened; the failures that are
   // not SQLite's (a wrong number of arguments, no connection, open() twice, a
   // refused statement, re-entrant use) have code 0.

   /**
    * @return true if the last operation failed, that is lastErrorKind() is not
    * DBErrorKind::None
    */
   bool hadError() const;

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

   /**
    * Whether a transaction is open on the connection, as SQLite itself says
    * (the inverse of sqlite3_get_autocommit): true after a successful BEGIN,
    * after a COMMIT that failed and left the transaction open, and after a
    * BEGIN run as plain SQL; false after COMMIT or ROLLBACK however they were
    * run, and on a database that is not open.
    */
   bool inTransaction();

   /**
    * Does nothing: whether a transaction is open is not something to set. Kept
    * so that existing code still builds.
    */
   void setInTransaction(bool flag);

   bool traceExecution();
   void setTraceExecution(bool flag);

   bool checkedOut();
   void setCheckedOut(bool flag);

   /**
    * How long, in milliseconds, an operation on this connection waits for
    * another connection's lock to go away before it fails with SQLITE_BUSY
    * (a failed query, update, transaction call or result set; never an
    * endless wait). The wait is done by SQLite itself
    * (sqlite3_busy_timeout), not by a retry loop in Chapeau.
    *
    * 0 means do not wait at all: fail at once. The default is
    * DEFAULT_BUSY_TIMEOUT_MS.
    */
   int busyRetryTimeout();

   /**
    * Sets the busy timeout, in milliseconds (previous versions counted retries
    * here, with 0 meaning forever). It may be set before open() -- it is then
    * applied when the connection is opened, and again if the database is
    * closed and reopened -- or at any time on an open connection. A negative
    * value is treated as 0.
    */
   void setBusyRetryTimeout(int newBusyRetryTimeout);

   static const int DEFAULT_BUSY_TIMEOUT_MS = 5000;

   static std::string sqliteLibVersion();


protected:
   void complainAboutInUse();

   // a statement that a live result set is still using is not deleted when
   // it leaves the cache; the result set becomes its owner
   void discardCachedStatement(DBStatement* statement);


private:
   friend class SQLiteResultSet;

   // Every failure of an operation goes through here: it is recorded as the
   // last error (see Database::lastErrorKind()) and, if setLogsErrors() is on,
   // written to stdout as one line with the kind, the code (where there is
   // one) and the message. 'operation' names the public call and 'detail' is
   // what it was given (the SQL), for the line only. Logging is the only thing
   // that setLogsErrors() changes; what is recorded and returned never depends
   // on it.
   void fail(DBErrorKind kind,
             int code,
             const std::string& message,
             const char* operation,
             const std::string& detail = std::string());

   // A failed sqlite3_step() (rc) in 'operation': records and logs it with
   // SQLite's message as it is now -- before the statement is reset or
   // finalized -- and returns that message, which a result set keeps.
   std::string stepFailed(const char* operation, int rc, const std::string& sql);

   // The number of arguments is not the number of the statement's parameters.
   // Nothing has been bound when this is called.
   void bindCountFailed(const char* operation,
                        const std::string& sql,
                        size_t parameterCount,
                        size_t argumentCount);

   // Binds each argument of args, in order, to statement. Stops at the first
   // one that cannot be bound and records it as DBErrorKind::BindFailed, with
   // SQLite's code and message if SQLite refused it; nothing has then been
   // executed. The caller releases the statement.
   bool bindArguments(const char* operation,
                      const std::string& sql,
                      const DBStatementArgs& args,
                      SQLiteStatement* statement);

   // The result of a sqlite3_bind_*() call: true for SQLITE_OK. Otherwise the
   // failure is recorded as BindFailed (not logged here: it is part of an
   // operation, whose own report names the parameter) and false is returned.
   bool bindResult(int rc);

   // The bind functions were not given a statement to bind to
   bool noStatementToBind();

   // prepares sql (sqlite3_prepare_v2) into *ppStmt; on failure *ppStmt is null
   // and the reason is recorded. A prepare that has to read the schema waits
   // for a lock for as long as the busy timeout allows.
   //
   // sql must hold at most one statement: a NUL byte, or anything but
   // whitespace, semicolons and comments after the first statement, is refused
   // (RefusedStatement) before anything is run or cached. Every statement that
   // is run or cached got here first, so the cache holds only such statements.
   bool prepareStatement(const std::string& sql,
                         sqlite3_stmt** ppStmt,
                         const char* operation);

   // Called with what sqlite3_prepare_v2() left after the first statement. Only
   // whitespace, semicolons and comments may follow it. Anything else -- a second
   // statement, or text that is not SQL -- is refused: returns true, after
   // recording it and finalizing *ppStmt (which is set to null). The trailing
   // text is prepared to tell what it is, and never run.
   bool refuseTrailingSql(const std::string& sql,
                          const char* tail,
                          sqlite3_stmt** ppStmt,
                          const char* operation);

   // The result set for a statement that is ready to run (prepared, with its
   // arguments bound). Runs the statement first if it has no result columns,
   // see executeQuery().
   DBResultSet* openResultSet(SQLiteStatement* statement, const std::string& sql);

   // executeUpdate() runs statements that do not return rows. Returns true, after
   // recording it, if pStmt has result columns and so must not be run; the
   // statement is finalized unless the cache owns it.
   bool refuseRowReturningUpdate(sqlite3_stmt* pStmt,
                                 const std::string& sql,
                                 bool ownedByCache);

   // Result sets created by this database that have not been closed or
   // destroyed yet. close() and the destructor invalidate each of them
   // (see SQLiteResultSet::invalidate) so that none outlives the connection
   // or the statements it uses. Raw pointers: the database owns neither the
   // result sets nor, through this set, anything else.
   //
   // Statement ownership (see also SQLiteResultSet):
   //  - the cache owns a cached statement while no result set uses it;
   //  - a result set has exclusive use of a statement while it is active
   //    (the statement's cursorCount() is 1). A statement that is in use is
   //    never reused or rebound: another execution of the same SQL gets a
   //    separate statement that is not cached, and belongs to its result set;
   //  - whoever drops the last reference to a statement the cache does not
   //    own deletes (and so finalizes) it.
   //
   // The states of a statement and what moves it between them:
   //   cached, idle       in the cache, cursorCount() 0.
   //                      -> cached, active: a result set is created on it
   //                      -> gone: the cache is cleared, or the database closes
   //   cached, active     in the cache, cursorCount() 1; not reused meanwhile.
   //                      -> cached, idle: its result set ends or is destroyed
   //                         (the statement is reset and its bindings cleared)
   //                      -> uncached, active: the cache is cleared (caching
   //                         switched off); the result set becomes the owner
   //                      -> gone: the database closes (the result set is
   //                         invalidated first)
   //   uncached, active   owned by its one result set (a separate statement
   //                      for a second execution of the same SQL, or one
   //                      prepared with caching off, or one the cache gave up).
   //                      -> gone: its result set ends, is destroyed, or is
   //                         invalidated by the database closing
   // Eviction from the cache (the limit lowered, or a new statement over the
   // limit) takes the least recently used statement out and hands it to
   // discardCachedStatement(), so it follows the same transitions as clearing
   // the cache: cached idle -> gone (finalized); cached active -> uncached
   // active, and its result set finalizes it when it ends. The limit counts the
   // cache's entries only; statements owned by result sets are not counted.
   // A result set is active until it ends (see SQLiteResultSet); an ended
   // result set holds no statement.
   //
   // The cached statement for sql if there is one and no active result set
   // uses it. slotTaken is set when there is one that is in use.
   SQLiteStatement* idleCachedStatement(const std::string& sql, bool& slotTaken);

   void registerCursor(SQLiteResultSet* resultSet);
   void unregisterCursor(SQLiteResultSet* resultSet);
   void invalidateCursors();

   std::set<SQLiteResultSet*> m_cursors;

   sqlite3*    m_db;
   std::string m_databasePath;
   bool        m_logsErrors;
   bool        m_crashOnErrors;
   bool        m_inUse;
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
   void clearBindings();

   sqlite3_stmt* statement();

   const std::string& query() const;
   void setQuery(const std::string& value);

   long useCount() const;
   void incrementUseCount();
   bool isCached() const;

   // The number of live result sets using this statement. A statement
   // that is not cached (or no longer is, see SQLiteDatabase::
   // discardCachedStatement) is deleted when the last of them lets go.
   int cursorCount() const;
   void attachCursor();
   int detachCursor();
   void setCached(bool value);

   std::string description() const;


private:
   sqlite3_stmt* m_statement;
   std::string m_query;
   long m_useCount;
   bool m_isCached;
   int m_cursorCount;

   SQLiteStatement();
   SQLiteStatement(const SQLiteStatement&);
   SQLiteStatement& operator=(const SQLiteStatement&);

};

}

#endif

