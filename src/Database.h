#ifndef CHAPEAU_DATABASE_H
#define CHAPEAU_DATABASE_H

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <list>
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <mutex>

namespace chapeau {

class DBNull;
class DBBool;
class DBInt;
class DBLong;
class DBFloat;
class DBDouble;
class DBString;
class DBDate;
class DBData;
class DBStatement;
class DBStatementArgs;
class DBResultSet;


enum DBValueType {
   ValueTypeInteger = 1,
   ValueTypeFloat   = 2,
   ValueTypeText    = 3,
   ValueTypeBlob    = 4,
   ValueTypeNull    = 5,
   ValueTypeLong    = 6,
   ValueTypeBoolean = 7,
   ValueTypeDouble  = 8,
   ValueTypeDate    = 9,
   ValueTypeTimestamp = 10,
   ValueTypeUnknown = 11
};

/**
 * Where a result set is in its life, as reported by DBResultSet::status().
 * Ready, then Row for as long as next() keeps returning true, and then exactly
 * one of the three final states, which never change afterwards.
 */
enum class DBResultSetStatus {
   Ready,        // created; next() has not been called yet
   Row,          // the last next() returned true: a row is available
   Done,         // the data is exhausted, or close() was called
   Error,        // the query failed; DBResultSet::lastErrorCode() says how
   Invalidated   // the database was closed or destroyed under it
};

/**
 * What kind of failure Database::lastErrorKind() reports. Sqlite is a failure
 * that SQLite itself reported, and then lastErrorCode() is SQLite's result
 * code. The other kinds are failures that are Chapeau's own: no SQLite call
 * failed, so lastErrorCode() is 0 for them -- except BindFailed, which carries
 * SQLite's code when it was a sqlite3_bind_*() call that failed.
 *
 * Compatibility: UnsupportedLibrary, the last enumerator, was added after
 * the others, which keep their values; the underlying type is unchanged. Source:
 * code that switches over DBErrorKind without a default case gets a
 * -Wswitch warning for the new enumerator until it handles it, and code
 * that treats "not None and not Sqlite" as 'Chapeau's own failure' now also
 * sees this one. Binary: compiled code that only compares kinds it knows is
 * unaffected; nothing else about the class layout changed.
 */
enum class DBErrorKind {
   None,             // the last operation succeeded
   Sqlite,           // SQLite reported an error (prepare, step, open, close)
   BindCount,        // the statement has a different number of parameters than the arguments given
   BindFailed,       // an argument could not be bound to its parameter
   NoConnection,     // there is no open connection
   AlreadyOpen,      // open() on a database that is already open
   InUse,            // the database was used while one of its operations was running
   RefusedStatement, // the SQL was refused without being run: a statement that returns rows given to
                     // executeUpdate(), or SQL with more than one statement or a NUL byte
   UnsupportedLibrary // open() was refused because the underlying library is not one Chapeau supports:
                      // SQLite compiled without threading support (sqlite3_threadsafe() is 0). Nothing
                      // was opened; the code is 0
};

/**
 * @return the name of the kind, such as "BindFailed"
 */
const char* dbErrorKindName(DBErrorKind kind);

/**
 * A database connection.
 *
 * Concurrency
 *
 * Chapeau is not thread safe. It supports one pattern, thread confinement
 * with explicit hand-off, and nothing else.
 *
 * A "unit" is one database connection (a Database), the result sets (cursors)
 * it has produced that have not been destroyed, and the prepared statements
 * they use (the statement cache and the statements owned by result sets). The
 * parts of a unit share state that nothing protects: the statement cache and
 * its order, the set of live result sets, the error snapshot, the in-use
 * marker, each statement's cursor count, and the underlying sqlite3*
 * connection. A result set reads and writes its database as it runs and as it
 * ends or is destroyed, and the database reads and writes its result sets when
 * it closes. The rules:
 *
 *  1. At most one thread may access any part of a unit at a time.
 *  2. Different database connections may be used concurrently by different
 *     threads (also to the same file: SQLite's own locks and the busy timeout
 *     coordinate them). Chapeau's own code keeps no mutable state that
 *     connections share (see also the process-wide sinks below).
 *  3. A unit may move between threads while it is idle (no call into it is
 *     running).
 *  4. A hand-off needs synchronization that the application provides: a
 *     mutex, a condition variable, a join, a queue. Chapeau provides none.
 *  5. Result sets move with the database they came from. A unit is moved
 *     whole: never the database to one thread and its cursors to another.
 *  6. Closing or destroying a database, and destroying a result set, are
 *     accesses to the unit like any other: none of them may overlap any other
 *     access to it. (A result set may be destroyed after its database was
 *     closed or destroyed; the hand-off rules still apply to that call.)
 *  7. The error snapshot (lastErrorKind(), lastErrorCode(),
 *     lastErrorMessage()) belongs to the unit. Read it before the next
 *     operation on the unit replaces it. A result set's own error
 *     (DBResultSet::lastErrorCode() ...) belongs to that result set and
 *     moves with it.
 *  8. A transaction belongs to the connection, not to a thread. It stays open
 *     when the unit moves, and the thread that has the unit may commit or roll
 *     it back.
 *  9. SQLite callbacks and hooks that the application installs through
 *     sqliteHandle() (user functions, update hooks, a busy handler, ...) run
 *     inside a Chapeau call, on that call's thread. They must not call back
 *     into the same unit, and must not wait for another thread to use it.
 * 10. Connection pooling is the application's responsibility. Chapeau has no
 *     pool; checkedOut() and setCheckedOut() are plain flags that Chapeau
 *     does not read.
 *
 * What this does not mean:
 *  - Unsynchronized access to one connection from several threads is not
 *    supported, whatever the operations are, reads included. A violation is a
 *    data race: undefined behavior, not a reported error.
 *  - SQLite's own thread mode does not change that. SQLite's serialized mode
 *    (sqlite3_threadsafe() == 1) protects SQLite's connection object; it does
 *    nothing for Chapeau's state around it, which is the part the rules above
 *    are about.
 *  - The in-use marker (SQLiteDatabase::inUse(), DBErrorKind::InUse) is not a
 *    lock. It is a flag that catches a call made from inside another call of
 *    the same database (re-entrance on one thread). Reading it is not a data
 *    race (it is atomic), but it does not wait, is set only by some of the
 *    database's calls (executeQuery, executeUpdate, lastInsertRowId; not
 *    next(), close() or open()), and is not a reliable detector of two threads:
 *    do not depend on it to find, or to survive, a violation.
 *  - What is safe on its own, without a unit: reading or setting a single
 *    setting (the logging, crash, trace and checked-out flags, the busy timeout
 *    of a database that is not open), reading statementCacheLimit() and
 *    shouldCacheStatements(), reading the error snapshot (each of the three
 *    getters returns a value that was published whole), and reading a result
 *    set's status() and, once that is final, its error. Nothing else is: the
 *    statement cache, the set of result sets, the statements and the
 *    connection are only for the one thread that has the unit.
 *  - Nothing in Chapeau enforces confinement. Keeping to the rules is the
 *    caller's responsibility. A later version may check some of them
 *    defensively; what is documented here is the contract either way.
 *  - The process-wide sinks are the application's: errors logged by
 *    setLogsErrors() go to stdout, and diagnostics go to Chaudiere's Logger.
 *    The Logger the application installs must itself tolerate being called from
 *    several threads if several connections are used at once.
 *  - SQLite must be built thread safe (sqlite3_threadsafe() not 0) for any use
 *    from more than one thread, including rule 2 and rule 3. SQLiteDatabase::
 *    open() refuses a library that is not (DBErrorKind::UnsupportedLibrary).
 *    That is a check of SQLite's compile-time mode only; it says nothing about
 *    SQLite's run-time configuration, and it does not make Chapeau thread
 *    safe: the rules above still apply in full to a library that passes it.
 */
class Database {

public:
   /**
    * Constructor
    */
   Database();

   /**
    * Destructor
    */
   virtual ~Database() {}

   /**
    *
    * @return
    */
   virtual long long int lastInsertRowId() = 0;

   /**
    *
    * @return
    */
   virtual bool commit() = 0;

   /**
    *
    * @return
    */
   virtual bool rollback() = 0;

   /**
    *
    * @param nullWrapper
    * @param idx
    * @param statement
    * @return
    * @see DBNull()
    * @see DBStatement()
    */
   virtual bool bind(const DBNull* nullWrapper,
                     int idx,
                     DBStatement* statement);

   /**
    *
    * @param boolWrapper
    * @param idx
    * @param statement
    * @return
    * @see DBBool()
    * @see DBStatement()
    */
   virtual bool bind(const DBBool* boolWrapper,
                     int idx,
                     DBStatement* statement);

   /**
    *
    * @param intWrapper
    * @param idx
    * @param statement
    * @return
    * @see DBInt()
    * @see DBStatement()
    */
   virtual bool bind(const DBInt* intWrapper,
                     int idx,
                     DBStatement* statement);

   /**
    *
    * @param longWrapper
    * @param idx
    * @param statement
    * @return
    * @see DBLong()
    * @see DBStatement()
    */
   virtual bool bind(const DBLong* longWrapper,
                     int idx,
                     DBStatement* statement);

   /**
    *
    * @param floatWrapper
    * @param idx
    * @param statement
    * @return
    * @see DBFloat()
    * @see DBStatement()
    */
   virtual bool bind(const DBFloat* floatWrapper,
                     int idx,
                     DBStatement* statement);

   /**
    *
    * @param doubleWrapper
    * @param idx
    * @param statement
    * @return
    * @see DBDouble()
    * @see DBStatement()
    */
   virtual bool bind(const DBDouble* doubleWrapper,
                     int idx,
                     DBStatement* statement);

   /**
    *
    * @param stringWrapper
    * @param idx
    * @param statement
    * @return
    * @see DBString()
    * @see DBStatement()
    */
   virtual bool bind(const DBString* stringWrapper,
                     int idx,
                     DBStatement* statement);

   /**
    *
    * @param date
    * @param idx
    * @param statement
    * @return
    * @see DBDate()
    * @see DBStatement()
    */
   virtual bool bind(const DBDate* date,
                     int idx,
                     DBStatement* statement);

   /**
    *
    * @param data
    * @param idx
    * @param statement
    * @return
    * @see DBData()
    * @see DBStatement()
    */
   virtual bool bind(const DBData* data,
                     int idx,
                     DBStatement* statement);

   /**
    *
    * @param data
    * @param idx
    * @param statement
    * @return
    * @see DBData()
    * @see DBStatement()
    */
   virtual bool bindData(const DBData* data,
                         int idx,
                         DBStatement* statement) = 0;

   /**
    *
    * @param value
    * @param idx
    * @param statement
    * @return
    * @see DBDate()
    * @see DBStatement()
    */
   virtual bool bindDate(const DBDate* value,
                         int idx,
                         DBStatement* statement) = 0;

   /**
    *
    * @param value
    * @param idx
    * @param statement
    * @return
    * @see DBStatement()
    */
   virtual bool bindBool(bool value,
                         int idx,
                         DBStatement* statement) = 0;

   /**
    *
    * @param value
    * @param idx
    * @param statement
    * @return
    * @see DBStatement()
    */
   virtual bool bindInt(int value,
                        int idx,
                        DBStatement* statement) = 0;

   /**
    *
    * @param value
    * @param idx
    * @param statement
    * @return
    * @see DBStatement()
    */
   virtual bool bindLong(long value,
                         int idx,
                         DBStatement* statement) = 0;

   /**
    *
    * @param value
    * @param idx
    * @param statement
    * @return
    * @see DBStatement()
    */
   virtual bool bindFloat(float value,
                          int idx,
                          DBStatement* statement) = 0;

   /**
    *
    * @param value
    * @param idx
    * @param statement
    * @return
    * @see DBStatement()
    */
   virtual bool bindDouble(double value,
                           int idx,
                           DBStatement* statement) = 0;

   /**
    *
    * @param value
    * @param idx
    * @param statement
    * @return
    * @see DBStatement()
    */
   virtual bool bindString(const std::string& value,
                           int idx,
                           DBStatement* statement) = 0;

   /**
    *
    * @param idx
    * @param statement
    * @return
    * @see DBStatement()
    */
   virtual bool bindNull(int idx, DBStatement* statement) = 0;

   /**
    * Runs a statement that does not return rows (INSERT, UPDATE, DELETE, DDL,
    * BEGIN/COMMIT, ...). A statement that does return rows -- a SELECT, a
    * PRAGMA that reads a value, INSERT ... RETURNING, EXPLAIN, VALUES -- is
    * refused without being run, even if it would return no rows; use
    * executeQuery() for those.
    *
    * One statement per call: sql may hold only one, and after it only
    * whitespace, semicolons and comments. A second statement, text after the
    * statement that is not valid SQL, or a NUL byte anywhere in sql is refused
    * as a whole (DBErrorKind::RefusedStatement, code 0, the message says where),
    * before anything is run: the first statement is not run either, and the
    * database is not changed. There is no call that runs a script; run each
    * statement with a call of its own.
    * @param sql
    * @param rowsAffectedCount the rows changed, or 0 when this returns false
    * @return true if the statement ran to completion; false if it failed or was
    * refused (a refused statement is not run at all)
    */
   virtual bool executeUpdate(const std::string& sql,
                              unsigned long& rowsAffectedCount) = 0;

   /**
    * Runs a statement with parameters that does not return rows (see the
    * overload above, which also says which statements are refused).
    * @param sql
    * @param args
    * Each argument is bound in order. If the number of arguments is not the
    * number of parameters (DBErrorKind::BindCount), or one cannot be bound
    * (DBErrorKind::BindFailed), the statement is not run: it never runs with a
    * parameter left unbound (NULL), and the call fails.
    * @param rowsAffectedCount the rows changed, or 0 when this returns false
    * @return true if the statement ran to completion; false if it failed or was
    * refused (a refused statement is not run at all); lastErrorKind() says why
    * @see DBStatementArgs()
    */
   virtual bool executeUpdate(const std::string& sql,
                              const DBStatementArgs& args,
                              unsigned long& rowsAffectedCount) = 0;

   /**
    * Prepares a statement without parameters and returns its result set.
    *
    * When the statement runs depends on whether it has result columns:
    *  - A statement that has none -- INSERT, UPDATE, DELETE, CREATE, DROP,
    *    BEGIN, COMMIT, a PRAGMA that only sets a value -- is run, to
    *    completion, before this returns, because there are no rows to step to.
    *    The result set is already finished: Done, or Error if the statement
    *    failed (its lastErrorCode() and lastErrorMessage() say how, and the
    *    database records the same, see lastErrorKind()); next() is false and
    *    never runs it again. It is not nullptr: nullptr is only for a
    *    statement that could not be prepared or bound.
    *  - A statement that has result columns -- SELECT, VALUES, EXPLAIN, a
    *    PRAGMA that returns a value, and INSERT/UPDATE/DELETE with RETURNING --
    *    is not run until the result set is stepped (see DBResultSet::next()),
    *    and destroying or closing it unstepped does not run it.
    *    Stepping a RETURNING statement performs the changes: the first step
    *    does all of them, before every returned row has been read. As far as
    *    other connections are concerned they are SQLite's to order -- not
    *    visible, and the write lock is held, until the statement is finished
    *    (the rows are exhausted, or the result set is closed or destroyed).
    * To run a statement and not keep a result set, executeUpdate() is the
    * call for one without rows.
    *
    * One statement per call: sql may hold only one, and after it only
    * whitespace, semicolons and comments. A second statement, text after the
    * statement that is not valid SQL, or a NUL byte anywhere in sql is refused
    * as a whole (DBErrorKind::RefusedStatement, code 0, the message says where),
    * before anything is run: the result is nullptr, the first statement is not
    * run, and the database is not changed. Empty SQL, or SQL of only
    * whitespace, semicolons and comments, is not refused: it has no statement,
    * and the result set is Done at once.
    * @param sql
    * @return the result set, or nullptr if the statement could not be
    * prepared or was refused; lastErrorKind() says why
    * @see DBResultSet()
    */
   virtual DBResultSet* executeQuery(const std::string& sql) = 0;

   /**
    * Prepares a statement with parameters and returns its result set. Each
    * argument is bound in order; if the number of arguments is not the number
    * of parameters (DBErrorKind::BindCount), or one cannot be bound
    * (DBErrorKind::BindFailed), nothing is run and there is no result set: it
    * is never run with a parameter left unbound (NULL). A statement without
    * result columns is run before this returns, and one with result columns is
    * run by stepping the result set, exactly as for the overload above, and
    * sql holds one statement at most, as there.
    * @param sql
    * @param args
    * @return the result set, or nullptr; lastErrorKind() says why
    * @see DBResultSet()
    */
   virtual DBResultSet* executeQuery(const std::string& sql,
                                     const DBStatementArgs& args) = 0;

   /**
    * Whether statements are being cached: the switch set by
    * setShouldCacheStatements() is on and statementCacheLimit() is not 0.
    */
   bool shouldCacheStatements() const;

   /**
    * Switches statement caching on (the default) or off. Switching it off
    * empties the cache; a statement a live result set is using is not
    * finalized, the result set keeps it until it ends. The cache limit is
    * kept, and applies again when caching is switched back on.
    */
   void setShouldCacheStatements(bool value);

   /**
    * The most statements the cache keeps. The default is
    * DEFAULT_STATEMENT_CACHE_LIMIT. 0 means no caching, as for
    * setShouldCacheStatements(false).
    */
   std::size_t statementCacheLimit() const;

   /**
    * Sets the most statements the cache keeps (see above). When the cache
    * is over the new limit the least recently used statements are evicted
    * at once, and whenever a new statement is cached it evicts the least
    * recently used one if the limit would be exceeded. A statement is used
    * when it is looked up for an execution. An evicted statement that no
    * result set is using is finalized; one that a live result set is still
    * using stays valid until that result set ends, and is then finalized.
    * The limit counts the cache's entries only: statements owned by result
    * sets are not counted.
    */
   void setStatementCacheLimit(std::size_t limit);

   static const std::size_t DEFAULT_STATEMENT_CACHE_LIMIT = 128;

   /**
    *
    * @param statement
    * @param query
    * @return
    * @see DBStatement()
    */
   void setCachedStatement(DBStatement* statement,
                           const std::string& query);

   /**
    *
    * @param query
    * @return
    * @see DBStatement()
    */
   DBStatement* cachedStatementForQuery(const std::string& query);

   /**
    *
    */
   void clearCachedStatements();

   /**
    *
    * @return
    */
   virtual bool open() = 0;

  /**
   *
   */
   virtual void close() = 0;

  /**
   *
   * @return
   */
   virtual bool goodConnection() = 0;

   /**
    * Why the last operation on this database failed. The error is a snapshot
    * kept by the database, not read from the underlying library when asked:
    *  - an operation that runs SQL or manages the connection (open, close,
    *    goodConnection, executeQuery, executeUpdate and the transaction calls
    *    built on it) starts by clearing the error;
    *  - one that succeeds leaves it cleared: kind None, code 0, message empty;
    *  - one that fails records its kind, code and message, which stay until the
    *    next such operation replaces them. Reading them does not clear them,
    *    and neither does anything the library does internally (releasing a
    *    statement, clearing bindings, emptying the cache, a cursor ending);
    *  - a result set that fails in next() also records its error here.
    * The code is the database's own result code (for SQLite, the result of the
    * failed call) where there is one, and 0 for a failure that did not come
    * from the database. A subclass that does not keep track reports None.
    * The three values are kept as one record behind a mutex of their own, so
    * reading or replacing the record never tears it (a message is never half of
    * one failure and half of another). That is all it guarantees: each of the
    * three getters locks separately, so a caller who reads all three while
    * another thread runs operations on the database may still get the parts of
    * different records, and the contract of the class (see above) is unchanged: a
    * database is used by one thread at a time, and the error is read before
    * the next operation replaces it.
    * @return the kind of the last failure
    */
   virtual DBErrorKind lastErrorKind() const;

   /**
    * @return the code of the last failure (see lastErrorKind()), 0 if none
    */
   virtual int lastErrorCode() const;

   /**
    * @return the message of the last failure (see lastErrorKind()), empty if
    * there was none
    */
   virtual std::string lastErrorMessage() const;

protected:
   /**
    * Records a failure as the last error (see lastErrorKind()), replacing the
    * previous one. For subclasses.
    */
   void setLastError(DBErrorKind kind, int code, const std::string& message);

   /**
    * Clears the last error: None, code 0, empty message. For subclasses.
    */
   void clearLastError();

   /**
    * The last error as one record: kind, code and message are those of one
    * failure (or of none), whatever other threads are recording meanwhile. For
    * subclasses.
    */
   struct LastError {
      DBErrorKind kind;
      int code;
      std::string message;
   };
   LastError lastErrorRecord() const;

   // The cache operations that the library's own code uses. The public
   // cachedStatementForQuery() and clearCachedStatements() do their work here
   // and nothing else, and SQLiteDatabase calls these, never the public ones.
   DBStatement* cachedStatementForQueryInternal(const std::string& query);
   void clearCachedStatementsInternal();

   /**
    * Called by clearCachedStatements() for each statement removed from the
    * cache. The default closes and deletes it; a subclass whose statements
    * can still be in use (by a live result set) overrides this to keep such
    * a statement alive until its last user is done with it.
    * @param statement a statement that is no longer in the cache
    */
   virtual void discardCachedStatement(DBStatement* statement);

private:
   // the cache: the statements by SQL text, and the SQL texts from the most
   // to the least recently used
   struct CacheEntry {
      DBStatement* statement;
      std::list<std::string>::iterator position;
   };

   // takes the least recently used statements out of the cache until at most
   // 'keep' are left, and hands each to discardCachedStatement()
   void evictLeastRecentlyUsed(std::size_t keep);

   std::map<std::string, CacheEntry> m_cachedStatements;
   std::list<std::string> m_cacheOrder;

   // The two settings of the cache are atomic so that statementCacheLimit() and
   // shouldCacheStatements() can be read without a data race while a setter
   // runs. That is all it is for: the setters also empty or trim the cache, and
   // the maps and lists above are NOT protected by it.
   std::atomic<std::size_t> m_statementCacheLimit;
   std::atomic<bool> m_shouldCacheStatements;

   // the last error (see lastErrorKind()): one record and the leaf mutex that
   // guards it. The mutex is held only while the record is copied or replaced,
   // never while calling SQLite, a callback, the logger or anything else.
   mutable std::mutex m_lastErrorMutex;
   LastError m_lastError;

};


/**
 *
 */
class DBResultSet {

private:
   std::string m_query;

public:
   /**
    * Destructor
    */
   virtual ~DBResultSet() {}

   /**
    *
    * @return
    */
   virtual bool next() = 0;

   /**
    *
    */
   virtual void close() = 0;

   /**
    * Why next() last returned what it did, which a bool alone cannot say.
    * A subclass that does not keep track reports Ready.
    * @return the status of this result set
    * @see DBResultSetStatus
    */
   virtual DBResultSetStatus status() const;

   /**
    * The error that ended this result set.
    * @return the database's own error code when status() is
    * DBResultSetStatus::Error (for SQLite, the result code of sqlite3_step),
    * otherwise 0
    */
   virtual int lastErrorCode() const;

   /**
    * The message of the error that ended this result set: the database's own
    * explanation (for SQLite, sqlite3_errmsg() at the moment the step failed),
    * when status() is DBResultSetStatus::Error, otherwise empty. Like the
    * code, it is a snapshot that stays after the statement is released, the
    * result set is exhausted or closed, and the database is closed.
    * @return the error message, empty unless status() is Error
    */
   virtual std::string lastErrorMessage() const;

   /**
    * The column as an int. A SQLite INTEGER outside the range of int
    * saturates to INT_MIN or INT_MAX; it never wraps. NULL reads as 0, and
    * REAL and TEXT values are converted as SQLite converts them (saturating
    * the same way). To tell NULL, a value that did not fit, or a value that is
    * not an integer apart, use the checked overload below.
    * @param columnName
    * @return
    */
   virtual int intForColumn(const std::string& columnName) const = 0;

   /**
    * The column as an int (see intForColumn).
    * @param columnIndex
    * @return
    */
   virtual int intForColumnIndex(int columnIdx) const = 0;

   /**
    * The column as a 64-bit integer, whatever the platform's long is.
    * The default implementation forwards to longForColumn(); a subclass whose
    * values can be wider than long overrides it.
    * @param columnName
    * @return
    */
   virtual std::int64_t int64ForColumn(const std::string& columnName) const;

   /**
    * The column as a 64-bit integer (see int64ForColumn).
    * @param columnIdx
    * @return
    */
   virtual std::int64_t int64ForColumnIndex(int columnIdx) const;

   /**
    * The column as an int, if it holds one.
    * @param columnName
    * @param value set to the value, or to 0 when this returns false
    * @return true only for an INTEGER value within the range of int; false for
    * NULL, REAL, TEXT, BLOB, an unknown column and an integer that does not
    * fit. It does not change the status of the result set. The default
    * implementation returns false: a subclass that can look a column up by
    * name overrides it.
    */
   virtual bool intForColumn(const std::string& columnName, int& value) const;

   /**
    * The column as an int, if it holds one (see the overload above).
    * @param columnIdx
    * @param value set to the value, or to 0 when this returns false
    * @return true only for an INTEGER value within the range of int
    */
   virtual bool intForColumnIndex(int columnIdx, int& value) const;

   /**
    *
    * @param columnName
    * @return
    */
   virtual long longForColumn(const std::string& columnName) const = 0;

   /**
    *
    * @param columnIdx
    * @return
    */
   virtual long longForColumnIndex(int columnIdx) const = 0;

   /**
    *
    * @param columnName
    * @return
    */
   virtual bool boolForColumn(const std::string& columnName) const = 0;

   /**
    *
    * @param columnIdx
    * @return
    */
   virtual bool boolForColumnIndex(int columnIdx) const = 0;

   /**
    *
    * @param columnName
    * @return
    */
   virtual double doubleForColumn(const std::string& columnName) const = 0;

   /**
    *
    * @param columnIdx
    * @return
    */
   virtual double doubleForColumnIndex(int columnIdx) const = 0;

   /**
    * The column as text, with all its bytes (NUL bytes included).
    * @param columnName
    * @return a new string the caller deletes, or nullptr for SQL NULL
    */
   virtual std::string* stringForColumn(const std::string& columnName) const = 0;

   /**
    * The column as text, with all its bytes (NUL bytes included).
    * @param columnIdx
    * @return a new string the caller deletes, or nullptr for SQL NULL
    */
   virtual std::string* stringForColumnIndex(int columnIdx) const = 0;

   /**
    * The column as text, with all its bytes (NUL bytes included).
    * @param columnName
    * @param s the text, or "" when this returns false
    * @return false for SQL NULL
    */
   virtual bool stringForColumn(const std::string& columnName, std::string& s) const = 0;

   /**
    * The column as text, with all its bytes (NUL bytes included).
    * @param columnIdx
    * @param s the text, or "" when this returns false
    * @return false for SQL NULL
    */
   virtual bool stringForColumnIndex(int columnIdx, std::string& s) const = 0;

   /**
    * The column as a date: seconds since the Unix epoch.
    * @param columnName
    * @return a new date the caller deletes; nullptr for SQL NULL, for an
    * unknown column and for a result set that has ended. The epoch itself is a
    * non-null DBDate of 0.0.
    */
   virtual DBDate* dateForColumn(const std::string& columnName) const = 0;

   /**
    * The column as a date: seconds since the Unix epoch.
    * @param columnIdx
    * @return a new date the caller deletes; nullptr for SQL NULL and for a
    * result set that has ended. The epoch itself is a non-null DBDate of 0.0.
    */
   virtual DBDate* dateForColumnIndex(int columnIdx) const = 0;

   /**
    * The column as a blob.
    * @param columnName
    * @return a new DBData the caller deletes; nullptr for SQL NULL, for an
    * unknown column and for a result set that has ended. A zero-length blob is
    * a non-null, empty DBData.
    */
   virtual DBData* dataForColumn(const std::string& columnName) const = 0;

   /**
    * The column as a blob.
    * @param columnIdx
    * @return a new DBData the caller deletes; nullptr for SQL NULL and for a
    * result set that has ended. A zero-length blob is a non-null, empty DBData.
    */
   virtual DBData* dataForColumnIndex(int columnIdx) const = 0;

   /**
    *
    * @return
    */
   virtual const std::string& query() const;

   /**
    *
    * @param query
    */
   virtual void setQuery(const std::string& query);

   virtual int getColumnCount() const = 0;

   virtual DBValueType getTypeForColumnIndex(int columnIndex) const = 0;
};

/**
 *
 */
class DBStatement {

public:
   /**
    * Destructor
    */
   virtual ~DBStatement() {}

   /**
    *
    * @param query
    */
   virtual void setQuery(const std::string& query) = 0;

   /**
    *
    */
   virtual void close() = 0;
};

}

#endif

