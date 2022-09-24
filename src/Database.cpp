// Database.cpp

#include "Database.h"
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
   m_shouldCacheStatements(true) {
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
   return m_shouldCacheStatements;
}

//******************************************************************************

void Database::setShouldCacheStatements(bool value) {
   m_shouldCacheStatements = value;

   if (!m_shouldCacheStatements) {
      clearCachedStatements();
   }
}

//******************************************************************************

DBStatement* Database::cachedStatementForQuery(const std::string& query) {
   map<string,DBStatement*>::iterator it = m_cachedStatements.find(query);
   if (it != m_cachedStatements.end()) {
      return (*it).second;
   }

   return NULL;
}

//******************************************************************************

void Database::setCachedStatement(DBStatement* statement,
                                  const std::string& query) {
   statement->setQuery(query);
   m_cachedStatements[query] = statement;
}

//******************************************************************************

void Database::clearCachedStatements() {
   map<string,DBStatement*>::iterator it = m_cachedStatements.begin();
   map<string,DBStatement*>::const_iterator itEnd = m_cachedStatements.end();

   for (; it != itEnd; ++it) {
      // close the statements
      DBStatement* statement = (*it).second;
      statement->close();
      delete statement;
   }

   m_cachedStatements.erase(m_cachedStatements.begin(),
                            m_cachedStatements.end());
}

//******************************************************************************
//******************************************************************************

const std::string& DBResultSet::query() const {
   return m_query;
}

//******************************************************************************

void DBResultSet::setQuery(const std::string& query) {
   m_query = query;
}

//******************************************************************************

