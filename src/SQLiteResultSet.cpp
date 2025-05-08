#include <stdio.h>
#include <unistd.h>

#include "SQLiteResultSet.h"
#include "SQLiteDatabase.h"
#include "StrUtils.h"
#include "DBData.h"
#include "DBDate.h"
#include "Logger.h"


using namespace chaudiere;
using namespace chapeau;

//******************************************************************************

SQLiteResultSet::SQLiteResultSet(SQLiteStatement* statement,
                                 SQLiteDatabase* db,
                                 const std::string& query) :
   m_parentDB(db),
   m_statement(statement),
   m_columnNamesSetup(false) {
   setQuery(query);
}

//******************************************************************************

SQLiteResultSet::~SQLiteResultSet() {
   close();
}

//******************************************************************************

void SQLiteResultSet::close() {
   if (m_statement != NULL) {
      m_statement->reset();
      if (!m_statement->isCached()) {
         delete m_statement;
         m_statement = NULL;
      }
   }
   if (m_parentDB != NULL) {
      m_parentDB->setInUse(false);
   }
}

//******************************************************************************

void SQLiteResultSet::setupColumnNames() {
   if (!m_columnNameToIndexMap.empty()) {
      m_columnNameToIndexMap.erase(m_columnNameToIndexMap.begin(),
                                   m_columnNameToIndexMap.end());
   }

   const int columnCount = sqlite3_column_count(m_statement->statement());

   for (int columnIdx = 0; columnIdx < columnCount; ++columnIdx) {
      std::string columnName =
         sqlite3_column_name(m_statement->statement(), columnIdx);
      StrUtils::toLowerCase(columnName);
      m_columnNameToIndexMap[columnName] = columnIdx;
   }

   m_columnNamesSetup = true;
}

//******************************************************************************

bool SQLiteResultSet::next() {
   int rc;
   bool retry;
   int numberOfRetries = 0;
   const int parentBusyRetryTimeout = m_parentDB->busyRetryTimeout();

   do {
      retry = false;

      rc = sqlite3_step(m_statement->statement());

      if (SQLITE_BUSY == rc) {
         // this will happen if the db is locked, like if we are doing an update or insert.
         // in that case, retry the step... and maybe wait just 10 milliseconds.
         retry = true;
         usleep(20);

         if (parentBusyRetryTimeout && (numberOfRetries++ > parentBusyRetryTimeout)) {
            ::printf("%s:%d Database busy (%s)\n",
                     __FUNCTION__,
                     __LINE__,
                     m_parentDB->databasePath().c_str());
            Logger::warning("Database busy");
            break;
         }
      } else if (SQLITE_DONE == rc || SQLITE_ROW == rc) {
         // all is well, let's return.
      } else if (SQLITE_ERROR == rc) {
         ::printf("Error calling sqlite3_step (%d: %s) rs\n",
                  rc,
                  sqlite3_errmsg(m_parentDB->sqliteHandle()));
         break;
      } else if (SQLITE_MISUSE == rc) {
         ::printf("Error calling sqlite3_step (%d: %s) rs\n",
                  rc,
                  sqlite3_errmsg(m_parentDB->sqliteHandle()));
         break;
      } else {
         ::printf("Unknown error calling sqlite3_step (%d: %s) rs\n",
                  rc,
                  sqlite3_errmsg(m_parentDB->sqliteHandle()));
         break;
      }
   } while (retry);

   if (rc != SQLITE_ROW) {
      close();
   }

   return (rc == SQLITE_ROW);
}

//******************************************************************************

int SQLiteResultSet::columnIndexForName(const std::string& columnName) const {
   if (!m_columnNamesSetup) {
      SQLiteResultSet* pThis = (SQLiteResultSet*) this;
      pThis->setupColumnNames();
   }

   std::string lowerColumnName = columnName;
   StrUtils::toLowerCase(lowerColumnName);

   const std::map<std::string, int>::const_iterator it =
      m_columnNameToIndexMap.find(lowerColumnName);

   if (it != m_columnNameToIndexMap.end()) {
      return (*it).second;
   }

   Logger::warning(std::string("Could not find the column named '") +
                   columnName +
                   std::string("'"));

   return -1;
}

//******************************************************************************

int SQLiteResultSet::intForColumn(const std::string& columnName) const {
   if (!m_columnNamesSetup) {
      SQLiteResultSet* pThis = (SQLiteResultSet*) this;
      pThis->setupColumnNames();
   }

   const int columnIdx = columnIndexForName(columnName);

   if (columnIdx == -1) {
      return 0;
   }

   return sqlite3_column_int(m_statement->statement(), columnIdx);
}

//******************************************************************************

int SQLiteResultSet::intForColumnIndex(int columnIdx) const {
   return sqlite3_column_int(m_statement->statement(), columnIdx);
}

//******************************************************************************

long SQLiteResultSet::longForColumn(const std::string& columnName) const {
   if (!m_columnNamesSetup) {
      SQLiteResultSet* pThis = (SQLiteResultSet*) this;
      pThis->setupColumnNames();
   }

   const int columnIdx = columnIndexForName(columnName);

   if (columnIdx == -1) {
      return 0;
   }

   return (long)sqlite3_column_int64(m_statement->statement(), columnIdx);
}

//******************************************************************************

long SQLiteResultSet::longForColumnIndex(int columnIdx) const {
   return (long)sqlite3_column_int64(m_statement->statement(), columnIdx);
}

//******************************************************************************

bool SQLiteResultSet::boolForColumn(const std::string& columnName) const {
   return (intForColumn(columnName) != 0);
}

//******************************************************************************

bool SQLiteResultSet::boolForColumnIndex(int columnIdx) const {
   return (intForColumnIndex(columnIdx) != 0);
}

//******************************************************************************

double SQLiteResultSet::doubleForColumn(const std::string& columnName) const {
   if (!m_columnNamesSetup) {
      SQLiteResultSet* pThis = (SQLiteResultSet*) this;
      pThis->setupColumnNames();
   }

   const int columnIdx = columnIndexForName(columnName);

   if (columnIdx == -1) {
      return 0;
   }

   return sqlite3_column_double(m_statement->statement(), columnIdx);
}

//******************************************************************************

double SQLiteResultSet::doubleForColumnIndex(int columnIdx) const {
   return sqlite3_column_double(m_statement->statement(), columnIdx);
}

//******************************************************************************

std::string* SQLiteResultSet::stringForColumnIndex(int columnIdx) const {
   const char *c =
      (const char *)sqlite3_column_text(m_statement->statement(), columnIdx);

   if (!c) {
      return NULL;
   }

   return new std::string(c);
}

//******************************************************************************

std::string* SQLiteResultSet::stringForColumn(const std::string& columnName) const {
   if (!m_columnNamesSetup) {
      SQLiteResultSet* pThis = (SQLiteResultSet*) this;
      pThis->setupColumnNames();
   }

   const int columnIdx = columnIndexForName(columnName);

   if (columnIdx == -1) {
      return NULL;
   }

   return stringForColumnIndex(columnIdx);
}

//******************************************************************************

DBDate* SQLiteResultSet::dateForColumn(const std::string& columnName) const {
   if (!m_columnNamesSetup) {
      SQLiteResultSet* pThis = (SQLiteResultSet*) this;
      pThis->setupColumnNames();
   }

   const int columnIdx = columnIndexForName(columnName);

   if (columnIdx == -1) {
      return NULL;
   }

   return dateForColumnIndex(columnIdx);
}

//******************************************************************************

DBDate* SQLiteResultSet::dateForColumnIndex(int columnIdx) const {
   return new DBDate(doubleForColumnIndex(columnIdx));
}

//******************************************************************************

DBData* SQLiteResultSet::dataForColumn(const std::string& columnName) const {
   if (!m_columnNamesSetup) {
      SQLiteResultSet* pThis = (SQLiteResultSet*) this;
      pThis->setupColumnNames();
   }

   const int columnIdx = columnIndexForName(columnName);

   if (columnIdx == -1) {
      return NULL;
   }

   return dataForColumnIndex(columnIdx);
}

//******************************************************************************

DBData* SQLiteResultSet::dataForColumnIndex(int columnIdx) const {
   const unsigned long dataSize =
      sqlite3_column_bytes(m_statement->statement(), columnIdx);
   const void* pBuffer = sqlite3_column_blob(m_statement->statement(),
                                             columnIdx);
   return new DBData(pBuffer, dataSize);
}

//******************************************************************************

void SQLiteResultSet::setParentDB(SQLiteDatabase* newDb) {
   m_parentDB = newDb;
}

//******************************************************************************

SQLiteStatement* SQLiteResultSet::statement() {
   return m_statement;
}

//******************************************************************************

void SQLiteResultSet::setStatement(SQLiteStatement* value) {
   m_statement = value;
}

//******************************************************************************

int SQLiteResultSet::getColumnCount() const {
   return sqlite3_column_count(m_statement->statement());
}

//******************************************************************************

DBValueType SQLiteResultSet::getTypeForColumnIndex(int columnIndex) const {
   const int colType = sqlite3_column_type(m_statement->statement(),
                                           columnIndex);
   DBValueType dbValueType;

   switch (colType) {
      case SQLITE_INTEGER:
         dbValueType = ValueTypeInteger;
         break;
      case SQLITE_FLOAT:
         dbValueType = ValueTypeFloat;
         break;
      case SQLITE_TEXT:
         dbValueType = ValueTypeText;
         break;
      case SQLITE_BLOB:
         dbValueType = ValueTypeBlob;
         break;
      case SQLITE_NULL:
         dbValueType = ValueTypeNull;
         break;
      default:
         dbValueType = ValueTypeUnknown;
	 break;
   }

   return dbValueType;
}

//******************************************************************************

