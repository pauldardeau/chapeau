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
   closeInternal();
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
   const DBResultSetStatus status = m_status.load(std::memory_order_acquire);
   return status == DBResultSetStatus::Done ||
          status == DBResultSetStatus::Error ||
          status == DBResultSetStatus::Invalidated;
}

//******************************************************************************

void SQLiteResultSet::finish(DBResultSetStatus finalStatus,
                             int errorCode,
                             const std::string& errorMessage) {
   // the first final state is the one that stays
   if (!isFinished()) {
      // the code and the message first, the status last and with release
      // semantics: whoever sees the final status also sees them (see m_status)
      m_errorCode = errorCode;
      m_errorMessage = errorMessage;
      m_status.store(finalStatus, std::memory_order_release);
   }

   // ends the iteration for good: once the statement is released there is
   // nothing left to step, so next() stays false and the query cannot restart
   releaseStatement();

   // (This used to end with m_parentDB->setInUse(false). A cursor ending is not
   // an operation of the database's, and it cleared the in-use marker of
   // whatever call of the database was running at the time. The marker is now
   // set and cleared only by the database call that owns it.)
}

//******************************************************************************

void SQLiteResultSet::close() {
   closeInternal();
}

//******************************************************************************

void SQLiteResultSet::closeInternal() {
   finish(DBResultSetStatus::Done, 0);
}

//******************************************************************************

DBResultSetStatus SQLiteResultSet::status() const {
   return m_status.load(std::memory_order_acquire);
}

//******************************************************************************

int SQLiteResultSet::lastErrorCode() const {
   return m_errorCode;
}

//******************************************************************************

std::string SQLiteResultSet::lastErrorMessage() const {
   return m_errorMessage;
}

//******************************************************************************

void SQLiteResultSet::invalidate() {
   // the database is closing or being destroyed: it has already taken this
   // result set out of its own list, and must not be called back. A result
   // set that had already ended keeps the state it ended in.
   if (!isFinished()) {
      m_errorCode = 0;
      m_errorMessage.clear();
      m_status.store(DBResultSetStatus::Invalidated, std::memory_order_release);
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
   return advance("next()");
}

//******************************************************************************

bool SQLiteResultSet::advance(const char* operation) {
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

   // a failed step is not the end of the data: say so, keep the error code and
   // SQLite's message for after the statement is released, and do not try the
   // query again. The database records the failure too (and logs it, if it is
   // set to).
   const std::string message = m_parentDB->stepFailed(operation, rc, query());
   finish(DBResultSetStatus::Error, rc, message);
   return false;
}

//******************************************************************************

// Every public accessor below does its work in a private '...Internal' twin and
// nothing else. The name-based accessors, and the ones defined in terms of
// another (boolFor* in terms of int64For*), call the Internal twins and never a
// public method, so that a future entry-point guard can be taken once, in the
// public method, without the method reaching it again through itself.

int SQLiteResultSet::columnIndexForName(const std::string& columnName) const {
   return columnIndexForNameInternal(columnName);
}

//******************************************************************************

int SQLiteResultSet::columnIndexForNameInternal(const std::string& columnName) const {
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
   const int columnIdx = columnIndexForNameInternal(columnName);

   if (columnIdx == -1) {
      return 0;
   }

   return intForColumnIndexInternal(columnIdx);
}

//******************************************************************************

int SQLiteResultSet::intForColumnIndex(int columnIdx) const {
   return intForColumnIndexInternal(columnIdx);
}

//******************************************************************************

int SQLiteResultSet::intForColumnIndexInternal(int columnIdx) const {
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
   const int columnIdx = columnIndexForNameInternal(columnName);

   if (columnIdx == -1) {
      return 0;
   }

   return int64ForColumnIndexInternal(columnIdx);
}

//******************************************************************************

std::int64_t SQLiteResultSet::int64ForColumnIndex(int columnIdx) const {
   return int64ForColumnIndexInternal(columnIdx);
}

//******************************************************************************

std::int64_t SQLiteResultSet::int64ForColumnIndexInternal(int columnIdx) const {
   sqlite3_stmt* const stmt = sqliteStatement();
   if (stmt == nullptr) {
      return 0;
   }

   return static_cast<std::int64_t>(sqlite3_column_int64(stmt, columnIdx));
}

//******************************************************************************

bool SQLiteResultSet::intForColumn(const std::string& columnName,
                                   int& value) const {
   const int columnIdx = columnIndexForNameInternal(columnName);

   if (columnIdx == -1) {
      value = 0;
      return false;
   }

   return intForColumnIndexInternal(columnIdx, value);
}

//******************************************************************************

bool SQLiteResultSet::intForColumnIndex(int columnIdx, int& value) const {
   return intForColumnIndexInternal(columnIdx, value);
}

//******************************************************************************

bool SQLiteResultSet::intForColumnIndexInternal(int columnIdx, int& value) const {
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
   const int columnIdx = columnIndexForNameInternal(columnName);

   if (columnIdx == -1) {
      return 0;
   }

   return longForColumnIndexInternal(columnIdx);
}

//******************************************************************************

long SQLiteResultSet::longForColumnIndex(int columnIdx) const {
   return longForColumnIndexInternal(columnIdx);
}

//******************************************************************************

long SQLiteResultSet::longForColumnIndexInternal(int columnIdx) const {
   sqlite3_stmt* const stmt = sqliteStatement();
   if (stmt == nullptr) {
      return 0;
   }

   return (long)sqlite3_column_int64(stmt, columnIdx);
}

//******************************************************************************

bool SQLiteResultSet::boolForColumn(const std::string& columnName) const {
   const int columnIdx = columnIndexForNameInternal(columnName);

   if (columnIdx == -1) {
      return false;
   }

   return (int64ForColumnIndexInternal(columnIdx) != 0);
}

//******************************************************************************

bool SQLiteResultSet::boolForColumnIndex(int columnIdx) const {
   return (int64ForColumnIndexInternal(columnIdx) != 0);
}

//******************************************************************************

double SQLiteResultSet::doubleForColumn(const std::string& columnName) const {
   const int columnIdx = columnIndexForNameInternal(columnName);

   if (columnIdx == -1) {
      return 0;
   }

   return doubleForColumnIndexInternal(columnIdx);
}

//******************************************************************************

double SQLiteResultSet::doubleForColumnIndex(int columnIdx) const {
   return doubleForColumnIndexInternal(columnIdx);
}

//******************************************************************************

double SQLiteResultSet::doubleForColumnIndexInternal(int columnIdx) const {
   sqlite3_stmt* const stmt = sqliteStatement();
   if (stmt == nullptr) {
      return 0;
   }

   return sqlite3_column_double(stmt, columnIdx);
}

//******************************************************************************

std::string* SQLiteResultSet::stringForColumnIndex(int columnIdx) const {
   return stringForColumnIndexInternal(columnIdx);
}

//******************************************************************************

std::string* SQLiteResultSet::stringForColumnIndexInternal(int columnIdx) const {
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
   const int columnIdx = columnIndexForNameInternal(columnName);

   if (columnIdx == -1) {
      return nullptr;
   }

   return stringForColumnIndexInternal(columnIdx);
}

//******************************************************************************

bool SQLiteResultSet::stringForColumn(const std::string& columnName,
                                      std::string& columnValue) const {
   const int columnIdx = columnIndexForNameInternal(columnName);

   if (columnIdx == -1) {
      columnValue = "";
      return false;
   } else {
      return stringForColumnIndexInternal(columnIdx, columnValue);
   }
}

//******************************************************************************

bool SQLiteResultSet::stringForColumnIndex(int columnIdx,
                                           std::string& columnValue) const {
   return stringForColumnIndexInternal(columnIdx, columnValue);
}

//******************************************************************************

bool SQLiteResultSet::stringForColumnIndexInternal(int columnIdx,
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
   const int columnIdx = columnIndexForNameInternal(columnName);

   if (columnIdx == -1) {
      return nullptr;
   }

   return dateForColumnIndexInternal(columnIdx);
}

//******************************************************************************

DBDate* SQLiteResultSet::dateForColumnIndex(int columnIdx) const {
   return dateForColumnIndexInternal(columnIdx);
}

//******************************************************************************

DBDate* SQLiteResultSet::dateForColumnIndexInternal(int columnIdx) const {
   sqlite3_stmt* const stmt = sqliteStatement();

   // SQL NULL is no date, which is not the same as the Unix epoch
   if (stmt == nullptr || sqlite3_column_type(stmt, columnIdx) == SQLITE_NULL) {
      return nullptr;
   }

   return new DBDate(sqlite3_column_double(stmt, columnIdx));
}

//******************************************************************************

DBData* SQLiteResultSet::dataForColumn(const std::string& columnName) const {
   const int columnIdx = columnIndexForNameInternal(columnName);

   if (columnIdx == -1) {
      return nullptr;
   }

   return dataForColumnIndexInternal(columnIdx);
}

//******************************************************************************

DBData* SQLiteResultSet::dataForColumnIndex(int columnIdx) const {
   return dataForColumnIndexInternal(columnIdx);
}

//******************************************************************************

DBData* SQLiteResultSet::dataForColumnIndexInternal(int columnIdx) const {
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

