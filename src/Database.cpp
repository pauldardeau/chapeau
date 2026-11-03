// Database.cpp

#include "Database.h"
#include "UnitState.h"
#include <climits>
#include <cstdint>
#include "DBNull.h"
#include "DBBool.h"
#include "DBInt.h"
#include "DBLong.h"
#include "DBFloat.h"
#include "DBDouble.h"
#include "DBString.h"
#include "DBDate.h"
#include "DBData.h"

using namespace std;
using namespace chapeau;

//******************************************************************************

Database::Database() :
   m_statementCacheLimit(DEFAULT_STATEMENT_CACHE_LIMIT),
   m_shouldCacheStatements(true),
   m_lastError{DBErrorKind::None, 0, std::string()},
   m_unit(std::make_shared<UnitState>()) {
}

//******************************************************************************

bool Database::bind(const DBNull* nullWrapper,
                    int idx,
                    DBStatement* statement) {
   return bindNull(idx, statement);
}

//******************************************************************************

bool Database::bind(const DBBool* boolWrapper,
                    int idx,
                    DBStatement* statement) {
   bool rc;
   if (!boolWrapper) {
      rc = bindNull(idx, statement);
   } else {
      rc = bindBool(boolWrapper->value(), idx, statement);
   }

   return rc;
}

//******************************************************************************

bool Database::bind(const DBInt* intWrapper,
                    int idx,
                    DBStatement* statement) {
   bool rc;
   if (!intWrapper) {
      rc = bindNull(idx, statement);
   } else {
      rc = bindInt(intWrapper->value(), idx, statement);
   }

   return rc;
}

//******************************************************************************

bool Database::bind(const DBLong* longWrapper,
                    int idx,
                    DBStatement* statement) {
   bool rc;
   if (!longWrapper) {
      rc = bindNull(idx, statement);
   } else {
      rc = bindLong(longWrapper->value(), idx, statement);
   }

   return rc;
}

//******************************************************************************

bool Database::bind(const DBFloat* floatWrapper,
                    int idx,
                    DBStatement* statement) {
   bool rc;
   if (!floatWrapper) {
      rc = bindNull(idx, statement);
   } else {
      rc = bindFloat(floatWrapper->value(), idx, statement);
   }

   return rc;
}

//******************************************************************************

bool Database::bind(const DBDouble* doubleWrapper,
                    int idx,
                    DBStatement* statement) {
   bool rc;
   if (!doubleWrapper) {
      rc = bindNull(idx, statement);
   } else {
      rc = bindDouble(doubleWrapper->value(), idx, statement);
   }

   return rc;
}

//******************************************************************************

bool Database::bind(const DBString* stringWrapper,
                    int idx,
                    DBStatement* statement) {
   bool rc;
   if (!stringWrapper) {
      rc = bindNull(idx, statement);
   } else {
      rc = bindString(stringWrapper->value(), idx, statement);
   }

   return rc;
}

//******************************************************************************

bool Database::bind(const DBData* data,
                    int idx,
                    DBStatement* statement) {
   bool rc;
   if (!data) {
      rc = bindNull(idx,statement);
   } else {
      rc = bindData(data, idx, statement);
   }

   return rc;
}

//******************************************************************************

bool Database::bind(const DBDate* date,
                    int idx,
                    DBStatement* statement) {
   bool rc;
   if (!date) {
      rc = bindNull(idx,statement);
   } else {
      rc = bindDate(date, idx, statement);
   }

   return rc;
}

//******************************************************************************

bool Database::shouldCacheStatements() const {
   return m_shouldCacheStatements && m_statementCacheLimit > 0;
}

//******************************************************************************

void Database::setShouldCacheStatements(bool value) {
   UnitClaim claim(unitStatePtr(), "setShouldCacheStatements");
   if (!claim.ok()) {
      return;
   }

   m_shouldCacheStatements = value;

   if (!value) {
      clearCachedStatementsInternal();
   }
}

//******************************************************************************

std::size_t Database::statementCacheLimit() const {
   return m_statementCacheLimit;
}

//******************************************************************************

void Database::setStatementCacheLimit(std::size_t limit) {
   UnitClaim claim(unitStatePtr(), "setStatementCacheLimit");
   if (!claim.ok()) {
      return;
   }

   m_statementCacheLimit = limit;

   if (limit == 0) {
      clearCachedStatementsInternal();
   } else {
      evictLeastRecentlyUsed(limit);
   }
}

//******************************************************************************

DBStatement* Database::cachedStatementForQuery(const std::string& query) {
   UnitClaim claim(unitStatePtr(), "cachedStatementForQuery");
   if (!claim.ok()) {
      return nullptr;
   }

   return cachedStatementForQueryInternal(query);
}

//******************************************************************************

DBStatement* Database::cachedStatementForQueryInternal(const std::string& query) {
   const auto it = m_cachedStatements.find(query);
   if (it == m_cachedStatements.end()) {
      return nullptr;
   }

   // looked up for an execution: now the most recently used
   m_cacheOrder.splice(m_cacheOrder.begin(), m_cacheOrder, it->second.position);
   return it->second.statement;
}

//******************************************************************************

void Database::setCachedStatement(DBStatement* statement,
                                  const std::string& query) {
   statement->setQuery(query);

   // a statement already cached for this SQL is replaced; it is handed to
   // discardCachedStatement() like any other statement that leaves the cache
   DBStatement* replaced = nullptr;
   const auto existing = m_cachedStatements.find(query);
   if (existing != m_cachedStatements.end()) {
      replaced = existing->second.statement;
      m_cacheOrder.erase(existing->second.position);
      m_cachedStatements.erase(existing);
   }

   m_cacheOrder.push_front(query);
   m_cachedStatements[query] = CacheEntry{statement, m_cacheOrder.begin()};

   if (replaced != nullptr && replaced != statement) {
      discardCachedStatement(replaced);
   }

   // the statement just added is the most recently used and is never the one
   // evicted, so the cache keeps at least it
   evictLeastRecentlyUsed(m_statementCacheLimit.load() > 0 ? m_statementCacheLimit.load() : std::size_t(1));
}

//******************************************************************************

void Database::evictLeastRecentlyUsed(std::size_t keep) {
   while (m_cachedStatements.size() > keep && !m_cacheOrder.empty()) {
      // out of the cache first, then handed on: nothing handed to
      // discardCachedStatement() is still reachable through the cache
      const std::string query = m_cacheOrder.back();
      m_cacheOrder.pop_back();

      const auto it = m_cachedStatements.find(query);
      if (it != m_cachedStatements.end()) {
         DBStatement* victim = it->second.statement;
         m_cachedStatements.erase(it);
         discardCachedStatement(victim);
      }
   }
}

//******************************************************************************

void Database::clearCachedStatements() {
   UnitClaim claim(unitStatePtr(), "clearCachedStatements");
   if (!claim.ok()) {
      return;
   }

   clearCachedStatementsInternal();
}

//******************************************************************************

void Database::clearCachedStatementsInternal() {
   // empty the cache first, so that nothing handed to
   // discardCachedStatement() is still reachable through it
   std::map<std::string, CacheEntry> cached;
   cached.swap(m_cachedStatements);
   m_cacheOrder.clear();

   for (auto& pair : cached) {
      discardCachedStatement(pair.second.statement);
   }
}

//******************************************************************************

void Database::discardCachedStatement(DBStatement* statement) {
   statement->close();
   delete statement;
}

//******************************************************************************

DBErrorKind Database::lastErrorKind() const {
   std::lock_guard<std::mutex> lock(m_lastErrorMutex);
   return m_lastError.kind;
}

//******************************************************************************

int Database::lastErrorCode() const {
   std::lock_guard<std::mutex> lock(m_lastErrorMutex);
   return m_lastError.code;
}

//******************************************************************************

std::string Database::lastErrorMessage() const {
   // the copy is made under the lock, the caller gets its own string
   std::lock_guard<std::mutex> lock(m_lastErrorMutex);
   return m_lastError.message;
}

//******************************************************************************

std::uint64_t Database::refusedCallsOnThisThread() {
   return UnitState::refusedCallsOnThisThread();
}

//******************************************************************************

std::uint64_t Database::refusedCalls() const {
   return m_unit->refusedCalls();
}

//******************************************************************************

std::shared_ptr<UnitState> Database::unitState() const {
   return m_unit;
}

//******************************************************************************

Database::LastError Database::lastErrorRecord() const {
   std::lock_guard<std::mutex> lock(m_lastErrorMutex);
   return m_lastError;
}

//******************************************************************************

void Database::setLastError(DBErrorKind kind,
                            int code,
                            const std::string& message) {
   std::lock_guard<std::mutex> lock(m_lastErrorMutex);
   m_lastError.kind = kind;
   m_lastError.code = code;
   m_lastError.message = message;
}

//******************************************************************************

void Database::clearLastError() {
   std::lock_guard<std::mutex> lock(m_lastErrorMutex);
   m_lastError.kind = DBErrorKind::None;
   m_lastError.code = 0;
   m_lastError.message.clear();
}

//******************************************************************************

const char* chapeau::dbErrorKindName(DBErrorKind kind) {
   switch (kind) {
      case DBErrorKind::None:             return "None";
      case DBErrorKind::Sqlite:           return "Sqlite";
      case DBErrorKind::BindCount:        return "BindCount";
      case DBErrorKind::BindFailed:       return "BindFailed";
      case DBErrorKind::NoConnection:     return "NoConnection";
      case DBErrorKind::AlreadyOpen:      return "AlreadyOpen";
      case DBErrorKind::InUse:            return "InUse";
      case DBErrorKind::RefusedStatement: return "RefusedStatement";
      case DBErrorKind::UnsupportedLibrary: return "UnsupportedLibrary";
   }
   return "Unknown";
}

//******************************************************************************
//******************************************************************************

DBResultSetStatus DBResultSet::status() const {
   return DBResultSetStatus::Ready;
}

//******************************************************************************

int DBResultSet::lastErrorCode() const {
   return 0;
}

//******************************************************************************

std::string DBResultSet::lastErrorMessage() const {
   return std::string();
}

//******************************************************************************

std::int64_t DBResultSet::int64ForColumn(const std::string& columnName) const {
   return longForColumn(columnName);
}

//******************************************************************************

std::int64_t DBResultSet::int64ForColumnIndex(int columnIdx) const {
   return longForColumnIndex(columnIdx);
}

//******************************************************************************

bool DBResultSet::intForColumn(const std::string&, int& value) const {
   // nothing here can look a column up by name: a subclass that can overrides
   value = 0;
   return false;
}

//******************************************************************************

bool DBResultSet::intForColumnIndex(int columnIdx, int& value) const {
   value = 0;

   if (getTypeForColumnIndex(columnIdx) != ValueTypeInteger) {
      return false;
   }

   const std::int64_t wide = int64ForColumnIndex(columnIdx);
   if (wide > INT_MAX || wide < INT_MIN) {
      return false;
   }

   value = static_cast<int>(wide);
   return true;
}

//******************************************************************************

const std::string& DBResultSet::query() const {
   return m_query;
}

//******************************************************************************

void DBResultSet::setQuery(const std::string& query) {
   m_query = query;
}

//******************************************************************************

