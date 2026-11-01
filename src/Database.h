#ifndef CHAPEAU_DATABASE_H
#define CHAPEAU_DATABASE_H

#include <cstddef>
#include <cstdint>
#include <list>
#include <string>
#include <vector>
#include <map>
#include <memory>

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
 *
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
    *
    * @param sql
    * @return
    */
   virtual bool executeUpdate(const std::string& sql,
                              unsigned long& rowsAffectedCount) = 0;

   /**
    *
    * @param sql
    * @param args
    * @return
    * @see DBStatementArgs()
    */
   virtual bool executeUpdate(const std::string& sql,
                              const DBStatementArgs& args,
                              unsigned long& rowsAffectedCount) = 0;

   /**
    *
    * @param sql
    * @return
    * @see DBResultSet()
    */
   virtual DBResultSet* executeQuery(const std::string& sql) = 0;

   /**
    *
    * @param sql
    * @param args
    * @return
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

protected:
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
   std::size_t m_statementCacheLimit;
   bool m_shouldCacheStatements;

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

