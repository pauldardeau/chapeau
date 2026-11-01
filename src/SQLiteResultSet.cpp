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

   if (m_statement != nullptr) {
      m_statement->attachCursor();
   }
   if (m_parentDB != nullptr) {
      m_parentDB->registerCursor(this);
   }
}

//******************************************************************************

SQLiteResultSet::~SQLiteResultSet() {
   close();
   releaseStatement();

   if (m_parentDB != nullptr) {
      m_parentDB->unregisterCursor(this);
      m_parentDB = nullptr;
   }
}

//******************************************************************************

void SQLiteResultSet::releaseStatement() {
   SQLiteStatement* statement = m_statement;
   m_statement = nullptr;

   if (statement != nullptr) {
      if (statement->detachCursor() == 0) {
         // the last user leaves the statement idle: no half-run execution,
         // no values bound for this execution
         statement->reset();
         statement->clearBindings();

         // and deletes (so finalizes) one that the cache does not own
         if (!statement->isCached()) {
            delete statement;
         }
      }
   }
}

//******************************************************************************

void SQLiteResultSet::close() {
   // ends the iteration for good: once the statement is released there is
   // nothing left to step, so next() stays false and the query cannot restart
   releaseStatement();

   if (m_parentDB != nullptr) {
      m_parentDB->setInUse(false);
   }
}

//******************************************************************************

void SQLiteResultSet::invalidate() {
   // the database is closing or being destroyed: it has already taken this
   // result set out of its own list, and must not be called back
   releaseStatement();
   m_parentDB = nullptr;
   m_columnNameToIndexMap.clear();
   m_columnNamesSetup = false;
}

//******************************************************************************

sqlite3_stmt* SQLiteResultSet::sqliteStatement() const {
   return (m_statement != nullptr) ? m_statement->statement() : nullptr;
}

//******************************************************************************

void SQLiteResultSet::setupColumnNames() const {
   if (!m_columnNameToIndexMap.empty()) {
      m_columnNameToIndexMap.erase(m_columnNameToIndexMap.begin(),
                                   m_columnNameToIndexMap.end());
   }

   sqlite3_stmt* const stmt = sqliteStatement();
   if (stmt == nullptr) {
      // invalid result set: there are no columns to name
      return;
   }

   const int columnCount = sqlite3_column_count(stmt);

   for (int columnIdx = 0; columnIdx < columnCount; ++columnIdx) {
      std::string columnName = sqlite3_column_name(stmt, columnIdx);
      StrUtils::toLowerCase(columnName);
      m_columnNameToIndexMap[columnName] = columnIdx;
   }

   m_columnNamesSetup = true;
}

//******************************************************************************

bool SQLiteResultSet::next() {
   sqlite3_stmt* const stmt = sqliteStatement();
   if (stmt == nullptr || m_parentDB == nullptr) {
      // exhausted, closed, or invalidated by the database closing: there is
      // nothing to step, and there never will be again
      return false;
   }

   int rc;
   bool retry;
   int numberOfRetries = 0;
   const int parentBusyRetryTimeout = m_parentDB->busyRetryTimeout();

   do {
      retry = false;

      rc = sqlite3_step(stmt);

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
      setupColumnNames();
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
      setupColumnNames();
   }

   const int columnIdx = columnIndexForName(columnName);

   if (columnIdx == -1) {
      return 0;
   }

   sqlite3_stmt* const stmt = sqliteStatement();
   if (stmt == nullptr) {
      return 0;
   }

   return sqlite3_column_int(stmt, columnIdx);
}

//******************************************************************************

int SQLiteResultSet::intForColumnIndex(int columnIdx) const {
   sqlite3_stmt* const stmt = sqliteStatement();
   if (stmt == nullptr) {
      return 0;
   }

   return sqlite3_column_int(stmt, columnIdx);
}

//******************************************************************************

long SQLiteResultSet::longForColumn(const std::string& columnName) const {
   if (!m_columnNamesSetup) {
      setupColumnNames();
   }

   const int columnIdx = columnIndexForName(columnName);

   if (columnIdx == -1) {
      return 0;
   }

   sqlite3_stmt* const stmt = sqliteStatement();
   if (stmt == nullptr) {
      return 0;
   }

   return (long)sqlite3_column_int64(stmt, columnIdx);
}

//******************************************************************************

long SQLiteResultSet::longForColumnIndex(int columnIdx) const {
   sqlite3_stmt* const stmt = sqliteStatement();
   if (stmt == nullptr) {
      return 0;
   }

   return (long)sqlite3_column_int64(stmt, columnIdx);
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
      setupColumnNames();
   }

   const int columnIdx = columnIndexForName(columnName);

   if (columnIdx == -1) {
      return 0;
   }

   sqlite3_stmt* const stmt = sqliteStatement();
   if (stmt == nullptr) {
      return 0;
   }

   return sqlite3_column_double(stmt, columnIdx);
}

//******************************************************************************

double SQLiteResultSet::doubleForColumnIndex(int columnIdx) const {
   sqlite3_stmt* const stmt = sqliteStatement();
   if (stmt == nullptr) {
      return 0;
   }

   return sqlite3_column_double(stmt, columnIdx);
}

//******************************************************************************

std::string* SQLiteResultSet::stringForColumnIndex(int columnIdx) const {
   sqlite3_stmt* const stmt = sqliteStatement();
   if (stmt == nullptr) {
      return nullptr;
   }

   const char *c = (const char *)sqlite3_column_text(stmt, columnIdx);

   if (!c) {
      return nullptr;
   }

   return new std::string(c);
}

//******************************************************************************

std::string* SQLiteResultSet::stringForColumn(const std::string& columnName) const {
   if (!m_columnNamesSetup) {
      setupColumnNames();
   }

   const int columnIdx = columnIndexForName(columnName);

   if (columnIdx == -1) {
      return nullptr;
   }

   return stringForColumnIndex(columnIdx);
}

//******************************************************************************

bool SQLiteResultSet::stringForColumn(const std::string& columnName,
                                      std::string& columnValue) const {
   if (!m_columnNamesSetup) {
      setupColumnNames();
   }

   const int columnIdx = columnIndexForName(columnName);

   if (columnIdx == -1) {
      columnValue = "";
      return false;
   } else {
      return stringForColumnIndex(columnIdx, columnValue);
   }
}

//******************************************************************************

bool SQLiteResultSet::stringForColumnIndex(int columnIdx,
                                           std::string& columnValue) const {
   sqlite3_stmt* const stmt = sqliteStatement();
   if (stmt == nullptr) {
      columnValue = "";
      return false;
   }

   const char *c = (const char *)sqlite3_column_text(stmt, columnIdx);

   if (c != nullptr) {
      columnValue = c;
      return true;
   } else {
      columnValue = "";
      return false;
   }
}

//******************************************************************************

DBDate* SQLiteResultSet::dateForColumn(const std::string& columnName) const {
   if (!m_columnNamesSetup) {
      setupColumnNames();
   }

   const int columnIdx = columnIndexForName(columnName);

   if (columnIdx == -1) {
      return nullptr;
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
      setupColumnNames();
   }

   const int columnIdx = columnIndexForName(columnName);

   if (columnIdx == -1) {
      return nullptr;
   }

   return dataForColumnIndex(columnIdx);
}

//******************************************************************************

DBData* SQLiteResultSet::dataForColumnIndex(int columnIdx) const {
   sqlite3_stmt* const stmt = sqliteStatement();
   if (stmt == nullptr) {
      return new DBData();
   }

   const unsigned long dataSize = sqlite3_column_bytes(stmt, columnIdx);
   const void* pBuffer = sqlite3_column_blob(stmt, columnIdx);
   return new DBData(pBuffer, dataSize);
}

//******************************************************************************

void SQLiteResultSet::setParentDB(SQLiteDatabase* newDb) {
   if (m_parentDB != nullptr) {
      m_parentDB->unregisterCursor(this);
   }
   m_parentDB = newDb;
   if (m_parentDB != nullptr) {
      m_parentDB->registerCursor(this);
   }
}

//******************************************************************************

SQLiteStatement* SQLiteResultSet::statement() {
   return m_statement;
}

//******************************************************************************

void SQLiteResultSet::setStatement(SQLiteStatement* value) {
   if (value == m_statement) {
      return;
   }
   releaseStatement();
   m_statement = value;
   if (m_statement != nullptr) {
      m_statement->attachCursor();
   }
}

//******************************************************************************

int SQLiteResultSet::getColumnCount() const {
   sqlite3_stmt* const stmt = sqliteStatement();
   if (stmt == nullptr) {
      return 0;
   }

   return sqlite3_column_count(stmt);
}

//******************************************************************************

DBValueType SQLiteResultSet::getTypeForColumnIndex(int columnIndex) const {
   sqlite3_stmt* const stmt = sqliteStatement();
   if (stmt == nullptr) {
      return ValueTypeNull;
   }

   const int colType = sqlite3_column_type(stmt, columnIndex);
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

