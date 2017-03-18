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
 *
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
    *
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
    *
    * @return
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
   
   
protected:
   /**
    *
    */
   void setupColumnNames();


private:
   SQLiteDatabase* m_parentDB;
   SQLiteStatement* m_statement;
   std::map<std::string, int> m_columnNameToIndexMap;
   bool m_columnNamesSetup;

   SQLiteResultSet();
   SQLiteResultSet(const SQLiteResultSet&);
   SQLiteResultSet& operator=(const SQLiteResultSet&);

};

}

#endif

