#include <stdio.h>

#include <climits>
#include <cstdint>

#include "SQLiteResultSet.h"
#include "SQLiteDatabase.h"
#include "StrUtils.h"
#include "DBData.h"
#include "DBDate.h"
#include "Logger.h"


using namespace chaudiere;
using namespace chapeau;

namespace {

// An int for a 64-bit value: saturates, never wraps. A value outside the range
// of int becomes INT_MIN or INT_MAX, so its sign and its order against other
// values survive.
int saturateToInt(sqlite3_int64 value) {
   if (value > INT_MAX) {
      return INT_MAX;
   }
   if (value < INT_MIN) {
      return INT_MIN;
   }
   return static_cast<int>(value);
}

}

//******************************************************************************

SQLiteResultSet::SQLiteResultSet(SQLiteStatement* statement,
                                 SQLiteDatabase* db,
                                 const std::string& query) :
   m_parentDB(db),
   m_statement(statement),
   m_columnNamesSetup(false),
   m_status(DBResultSetStatus::Ready),
   m_errorCode(0) {
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

bool SQLiteResultSet::isFinished() const {
   return m_status == DBResultSetStatus::Done ||
          m_status == DBResultSetStatus::Error ||
          m_status == DBResultSetStatus::Invalidated;
}

//******************************************************************************

void SQLiteResultSet::finish(DBResultSetStatus finalStatus, int errorCode) {
   // the first final state is the one that stays
   if (!isFinished()) {
      m_status = finalStatus;
      m_errorCode = errorCode;
   }

   // ends the iteration for good: once the statement is released there is
   // nothing left to step, so next() stays false and the query cannot restart
   releaseStatement();

   if (m_parentDB != nullptr) {
      m_parentDB->setInUse(false);
   }
}

//******************************************************************************

void SQLiteResultSet::close() {
   finish(DBResultSetStatus::Done, 0);
}

//******************************************************************************

DBResultSetStatus SQLiteResultSet::status() const {
   return m_status;
}

//******************************************************************************

int SQLiteResultSet::lastErrorCode() const {
   return m_errorCode;
}

//******************************************************************************

void SQLiteResultSet::invalidate() {
   // the database is closing or being destroyed: it has already taken this
   // result set out of its own list, and must not be called back. A result
   // set that had already ended keeps the state it ended in.
   if (!isFinished()) {
      m_status = DBResultSetStatus::Invalidated;
      m_errorCode = 0;
   }
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
   if (isFinished()) {
      // done, failed, or invalidated by the database closing: there is
      // nothing to step, and there never will be again
      return false;
   }

   sqlite3_stmt* const stmt = sqliteStatement();
   if (stmt == nullptr || m_parentDB == nullptr) {
      // no statement to run (a result set made without one)
      finish(DBResultSetStatus::Done, 0);
      return false;
   }

   // sqlite3_step() waits for a lock for as long as the database's busy
   // timeout allows (see SQLiteDatabase::setBusyRetryTimeout) and then fails
   // with SQLITE_BUSY like any other error
   const int rc = sqlite3_step(stmt);

   if (SQLITE_ROW == rc) {
      m_status = DBResultSetStatus::Row;
      return true;
   }

   if (SQLITE_DONE == rc) {
      finish(DBResultSetStatus::Done, 0);
      return false;
   }

   // a failed step is not the end of the data: say so, keep the error code
   // for after the statement is released, and do not try the query again
   m_parentDB->reportStepError("next()", rc, query());
   finish(DBResultSetStatus::Error, rc);
   return false;
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
   const int columnIdx = columnIndexForName(columnName);

   if (columnIdx == -1) {
      return 0;
   }

   return intForColumnIndex(columnIdx);
}

//******************************************************************************

int SQLiteResultSet::intForColumnIndex(int columnIdx) const {
   sqlite3_stmt* const stmt = sqliteStatement();
   if (stmt == nullptr) {
      return 0;
   }

   // REAL and TEXT values are converted by SQLite as before (a REAL is
   // truncated towards zero, a TEXT is read as a number); what changes is
   // only that a result outside the range of int saturates instead of wrapping
   return saturateToInt(sqlite3_column_int64(stmt, columnIdx));
}

//******************************************************************************

std::int64_t SQLiteResultSet::int64ForColumn(const std::string& columnName) const {
   const int columnIdx = columnIndexForName(columnName);

   if (columnIdx == -1) {
      return 0;
   }

   return int64ForColumnIndex(columnIdx);
}

//******************************************************************************

std::int64_t SQLiteResultSet::int64ForColumnIndex(int columnIdx) const {
   sqlite3_stmt* const stmt = sqliteStatement();
   if (stmt == nullptr) {
      return 0;
   }

   return static_cast<std::int64_t>(sqlite3_column_int64(stmt, columnIdx));
}

//******************************************************************************

bool SQLiteResultSet::intForColumn(const std::string& columnName,
                                   int& value) const {
   const int columnIdx = columnIndexForName(columnName);

   if (columnIdx == -1) {
      value = 0;
      return false;
   }

   return intForColumnIndex(columnIdx, value);
}

//******************************************************************************

bool SQLiteResultSet::intForColumnIndex(int columnIdx, int& value) const {
   value = 0;

   sqlite3_stmt* const stmt = sqliteStatement();
   if (stmt == nullptr || columnIdx < 0 || columnIdx >= sqlite3_column_count(stmt)) {
      return false;
   }

   // only an INTEGER that fits: NULL, REAL, TEXT and BLOB are refused rather
   // than converted. (The type is asked before anything converts the value.)
   if (sqlite3_column_type(stmt, columnIdx) != SQLITE_INTEGER) {
      return false;
   }

   const sqlite3_int64 wide = sqlite3_column_int64(stmt, columnIdx);
   if (wide > INT_MAX || wide < INT_MIN) {
      return false;
   }

   value = static_cast<int>(wide);
   return true;
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
   return (int64ForColumn(columnName) != 0);
}

//******************************************************************************

bool SQLiteResultSet::boolForColumnIndex(int columnIdx) const {
   return (int64ForColumnIndex(columnIdx) != 0);
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

   return new std::string(c, static_cast<size_t>(sqlite3_column_bytes(stmt, columnIdx)));
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
      columnValue.assign(c, static_cast<size_t>(sqlite3_column_bytes(stmt, columnIdx)));
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
   sqlite3_stmt* const stmt = sqliteStatement();

   // SQL NULL is no date, which is not the same as the Unix epoch
   if (stmt == nullptr || sqlite3_column_type(stmt, columnIdx) == SQLITE_NULL) {
      return nullptr;
   }

   return new DBDate(sqlite3_column_double(stmt, columnIdx));
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

   // SQL NULL is no blob, which is not the same as a zero-length blob
   if (stmt == nullptr || sqlite3_column_type(stmt, columnIdx) == SQLITE_NULL) {
      return nullptr;
   }

   // the pointer first and the size after it, in the order SQLite documents
   const void* pBuffer = sqlite3_column_blob(stmt, columnIdx);
   const int dataSize = sqlite3_column_bytes(stmt, columnIdx);
   if (pBuffer == nullptr || dataSize <= 0) {
      return new DBData();   // a zero-length blob: empty, and not NULL
   }

   return new DBData(pBuffer, static_cast<unsigned long>(dataSize));
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

