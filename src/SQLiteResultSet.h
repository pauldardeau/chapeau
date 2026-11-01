#ifndef CHAPEAU_SQLITERESULTSET_H
#define CHAPEAU_SQLITERESULTSET_H

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
 * Lifetime: a result set may be destroyed at any time, before or after the
 * database it came from. When the database is closed or destroyed first, the
 * result set becomes invalid: next() returns false, and every column accessor
 * returns what it returns for a NULL column (0, false, 0.0, nullptr/false for
 * strings, an empty DBData, a DBDate of 0.0, getColumnCount() 0,
 * ValueTypeNull, columnIndexForName() -1). Turning statement caching off does
 * not affect a live result set. Single-threaded: a result set and its
 * database must not be used from different threads at the same time.
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
    * Ends the iteration: resets the statement and, if the result set is its
    * only owner, deletes it. Safe to call more than once, and safe after the
    * database was closed or destroyed (then there is nothing left to do).
    */
   void close();

   /**
    *
    * @param columnName
    * @return
    */
   int columnIndexForName(const std::string& columnName) const;

   /**
    *
    * @return
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

   /**
    *
    * @param columnName
    * @return
    */
   int intForColumn(const std::string& columnName) const;

   /**
    *
    * @param columnIdx
    * @return
    */
   int intForColumnIndex(int columnIdx) const;

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

   // the sqlite3 statement to read from, or nullptr if there is none
   sqlite3_stmt* sqliteStatement() const;

   SQLiteDatabase* m_parentDB;
   SQLiteStatement* m_statement;
   mutable std::map<std::string, int> m_columnNameToIndexMap;
   mutable bool m_columnNamesSetup;
   bool m_invalidated;

   SQLiteResultSet();
   SQLiteResultSet(const SQLiteResultSet&);
   SQLiteResultSet& operator=(const SQLiteResultSet&);

};

}

#endif

