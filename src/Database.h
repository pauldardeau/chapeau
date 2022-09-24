#ifndef CHAPEAU_DATABASE_H
#define CHAPEAU_DATABASE_H

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
    *
    * @return
    */
   bool shouldCacheStatements() const;

   /**
    *
    * @param value
    * @return
    */
   void setShouldCacheStatements(bool value);

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

private:
   std::map<std::string, DBStatement*> m_cachedStatements;
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
    *
    * @param columnName
    * @return
    */
   virtual int intForColumn(const std::string& columnName) const = 0;

   /**
    *
    * @param columnIndex
    * @return
    */
   virtual int intForColumnIndex(int columnIdx) const = 0;

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
    *
    * @param columnName
    * @return
    */
   virtual std::string* stringForColumn(const std::string& columnName) const = 0;

   /**
    *
    * @param columnIdx
    * @return
    */
   virtual std::string* stringForColumnIndex(int columnIdx) const = 0;

   /**
    *
    * @param columnName
    * @return
    */
   virtual DBDate* dateForColumn(const std::string& columnName) const = 0;

   /**
    *
    * @param columnIdx
    * @return
    */
   virtual DBDate* dateForColumnIndex(int columnIdx) const = 0;

   /**
    *
    * @param columnName
    * @return
    */
   virtual DBData* dataForColumn(const std::string& columnName) const = 0;

   /**
    *
    * @param columnIdx
    * @return
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

