#ifndef CHAPEAU_SQLITERESULTSET_H
#define CHAPEAU_SQLITERESULTSET_H

#include <cstdint>
#include <string>
#include <map>

#include "Database.h"
#include "sqlite3.h"


namespace chapeau {

class SQLiteDatabase;
class SQLiteStatement;
class Data;
class Date;


/**
 * A cursor over the rows produced by one query.
 *
 * A result set is active until it ends, and has exclusive use of its prepared
 * statement while it is: another result set over the same SQL, even with the
 * same parameters, runs on a statement of its own and the two do not disturb
 * each other.
 *
 * It ends, for good, when next() returns false (the data is exhausted, or an
 * error or busy-timeout stopped it), when close() is called, or when the
 * database it came from is closed or destroyed. status() says which:
 *
 *   new result set                         Ready,       lastErrorCode() 0
 *   next() returned true                   Row,         0
 *   data exhausted (next() false)          Done,        0
 *   close() on an active result set        Done,        0
 *   sqlite3_step failed, busy included     Error,       the sqlite3_step result,
 *                                                       and lastErrorMessage()
 *   database closed or destroyed under an
 *     active result set                    Invalidated, 0
 *
 * The first of these final states is the one that stays: a result set that
 * already ended as Done or Error is not turned into Invalidated by the
 * database closing later, and its error code is kept. next() on an ended
 * result set is false, whatever the reason it ended.
 *
 * An ended result set has let go
 * of its statement: next() keeps returning false (the query never restarts),
 * close() may be repeated, and every column accessor returns what it returns
 * for a NULL column: 0, false, 0.0, nullptr (or false) for strings, nullptr
 * for dates and blobs, false from the checked integer accessors,
 * getColumnCount() 0, ValueTypeNull, columnIndexForName() -1.
 *
 * SQL NULL and a real value are told apart where the accessor returns a
 * pointer: stringForColumn*, dateForColumn* and dataForColumn* return nullptr
 * for NULL. A zero-length BLOB is a non-null, empty DBData, and the Unix epoch
 * is a non-null DBDate of 0.0. The accessors that return a number cannot say
 * NULL: use getTypeForColumnIndex(), or the checked intForColumn*(…, int&).
 * Text and blob values are returned with all their bytes, NUL bytes included.
 *
 * A result set may be destroyed at any time, before
 * or after the database it came from, and is not revived by the database
 * being opened again. Turning statement caching off does not affect an active
 * result set. Single-threaded: a result set and its database must not be used
 * from different threads at the same time.
 */
class SQLiteResultSet : public DBResultSet {

public:
   /**
    *
    * @param statement
    * @param db
    * @param query
    */
   SQLiteResultSet(SQLiteStatement* statement,
                   SQLiteDatabase* db,
                   const std::string& query);

   /**
    * Destructor
    */
   ~SQLiteResultSet();

   /**
    * Ends the iteration: lets go of the statement (reset, with its bound
    * values cleared; deleted if no one else owns it and the cache does not).
    * Safe to call more than once, and safe after the database was closed or
    * destroyed (then there is nothing left to do).
    */
   void close();

   /**
    * @return the status of this result set (see the class comment)
    */
   DBResultSetStatus status() const;

   /**
    * @return the sqlite3_step result that ended this result set when
    * status() is DBResultSetStatus::Error (SQLITE_BUSY if the database stayed
    * locked for longer than the busy timeout), otherwise 0. Kept after the
    * statement was released.
    */
   int lastErrorCode() const;

   /**
    * @return SQLite's message for the failure that ended this result set
    * ("UNIQUE constraint failed: t.id", "database is locked") when status() is
    * DBResultSetStatus::Error, otherwise empty. It is copied when the step
    * fails, so it stays, like the code, after the statement is released, after
    * next() is called again, after close(), after the database is closed or
    * destroyed, and whatever the database is used for afterwards.
    */
   std::string lastErrorMessage() const;

   /**
    *
    * @param columnName
    * @return
    */
   int columnIndexForName(const std::string& columnName) const;

   /**
    *
    * @return the statement this result set is using, or nullptr once the
    * result set has ended (see the class comment)
    * @see SQLiteStatement()
    */
   SQLiteStatement* statement();

   /**
    *
    * @param value
    * @see SQLiteStatement()
    */
   void setStatement(SQLiteStatement* value);

   /**
    *
    * @param newDb
    * @see SQLiteDatabase()
    */
   void setParentDB(SQLiteDatabase* newDb);

   /**
    * Steps to the next row.
    * @return true if there is a row; false at the end of the data, on an
    * error, and always once the database this result set came from has been
    * closed or destroyed
    */
   bool next();

   // the overloads of the base class that are not overridden here (the
   // 64-bit and the checked accessors) stay visible through this class
   using DBResultSet::intForColumn;
   using DBResultSet::intForColumnIndex;

   /**
    * The column as an int; a value outside the range of int saturates to
    * INT_MIN or INT_MAX, it does not wrap (see DBResultSet::intForColumn).
    * @param columnName
    * @return
    */
   int intForColumn(const std::string& columnName) const;

   /**
    * The column as an int (see intForColumn).
    * @param columnIdx
    * @return
    */
   int intForColumnIndex(int columnIdx) const;

   /**
    * The column as a 64-bit integer: sqlite3_column_int64, at full width on
    * every platform.
    * @param columnName
    * @return
    */
   std::int64_t int64ForColumn(const std::string& columnName) const;

   /**
    * The column as a 64-bit integer (see int64ForColumn).
    * @param columnIdx
    * @return
    */
   std::int64_t int64ForColumnIndex(int columnIdx) const;

   /**
    * The column as an int, if it is a SQLite INTEGER that fits.
    * @param columnName
    * @param value the value, or 0 when this returns false
    * @return false for NULL, REAL, TEXT, BLOB, an unknown column, an integer
    * outside the range of int, and a result set that has ended
    */
   bool intForColumn(const std::string& columnName, int& value) const;

   /**
    * The column as an int, if it is a SQLite INTEGER that fits.
    * @param columnIdx
    * @param value the value, or 0 when this returns false
    * @return false for NULL, REAL, TEXT, BLOB, a column that does not exist,
    * an integer outside the range of int, and a result set that has ended
    */
   bool intForColumnIndex(int columnIdx, int& value) const;

   /**
    *
    * @param columnName
    * @return
    */
   long longForColumn(const std::string& columnName) const;

   /**
    *
    * @param columnIdx
    * @return
    */
   long longForColumnIndex(int columnIdx) const;

   /**
    *
    * @param columnName
    * @return
    */
   bool boolForColumn(const std::string& columnName) const;

   /**
    *
    * @param columnIdx
    * @return
    */
   bool boolForColumnIndex(int columnIdx) const;

   /**
    *
    * @param columnName
    * @return
    */
   double doubleForColumn(const std::string& columnName) const;

   /**
    *
    * @param columnIdx
    * @return
    */
   double doubleForColumnIndex(int columnIdx) const;

   /**
    *
    * @param columnName
    * @return
    */
   std::string* stringForColumn(const std::string& columnName) const;

   /**
    *
    * @param columnIdx
    * @return
    */
   std::string* stringForColumnIndex(int columnIdx) const;

   /**
    *
    * @param columnName
    * @param columnValue
    * @return
    */
   bool stringForColumn(const std::string& columnName, std::string& columnValue) const;

   /**
    *
    * @param columnIdx
    * @param columnValue
    * @return
    */
   bool stringForColumnIndex(int columnIdx, std::string& columnValue) const;

   /**
    *
    * @param columnName
    * @return
    * @see DBDate()
    */
   DBDate* dateForColumn(const std::string& columnName) const;

   /**
    *
    * @param columnIdx
    * @return
    * @see DBDate()
    */
   DBDate* dateForColumnIndex(int columnIdx) const;

   /**
    *
    * @param columnName
    * @return
    * @see DBData()
    */
   DBData* dataForColumn(const std::string& columnName) const;

   /**
    *
    * @param columnIdx
    * @return
    * @see DBData()
    */
   DBData* dataForColumnIndex(int columnIdx) const;

   int getColumnCount() const;

   DBValueType getTypeForColumnIndex(int columnIndex) const;


protected:
   /**
    *
    */
   void setupColumnNames() const;


private:
   friend class SQLiteDatabase;

   // Called by the parent database when it is closed or destroyed. The
   // result set lets go of its statement (finalizing it if it is the
   // last user and the cache does not own it), forgets the database, and
   // is invalid from then on: next() returns false and the column
   // accessors return what they return for a NULL column. It is not
   // reactivated by the database being opened again.
   void invalidate();

   // gives up this result set's use of its statement; a statement that is
   // not cached and has no other user is deleted
   void releaseStatement();

   // ends the result set: the first final state wins (see the class comment),
   // then the statement is released
   void finish(DBResultSetStatus finalStatus,
               int errorCode,
               const std::string& errorMessage = std::string());

   bool isFinished() const;

   // the sqlite3 statement to read from, or nullptr if there is none
   sqlite3_stmt* sqliteStatement() const;

   SQLiteDatabase* m_parentDB;
   SQLiteStatement* m_statement;
   mutable std::map<std::string, int> m_columnNameToIndexMap;
   mutable bool m_columnNamesSetup;
   DBResultSetStatus m_status;
   int m_errorCode;
   std::string m_errorMessage;

   SQLiteResultSet();
   SQLiteResultSet(const SQLiteResultSet&);
   SQLiteResultSet& operator=(const SQLiteResultSet&);

};

}

#endif

