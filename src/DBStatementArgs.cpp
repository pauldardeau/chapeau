#include "DBStatementArgs.h"
#include "DBAbstractDataType.h"

//******************************************************************************

DBStatementArgs::DBStatementArgs() {
}

//******************************************************************************

DBStatementArgs::~DBStatementArgs() {
   clear();
}

//******************************************************************************

void DBStatementArgs::clear() {
   std::vector<DBAbstractDataType*>::iterator it = m_args.begin();
   const std::vector<DBAbstractDataType*>::const_iterator itEnd = m_args.end();

   for (; it != itEnd; ++it) {
      delete *it;
   }
}

//******************************************************************************

std::size_t DBStatementArgs::size() const {
   return m_args.size();
}

//******************************************************************************

bool DBStatementArgs::empty() const {
   return m_args.empty();
}

//******************************************************************************

void DBStatementArgs::add(DBAbstractDataType* arg) {
   m_args.push_back(arg);
}

//******************************************************************************

DBAbstractDataType* DBStatementArgs::operator[](std::size_t pos) {
   return m_args[pos];
}

//******************************************************************************

const DBAbstractDataType* DBStatementArgs::operator[](std::size_t pos) const {
   return m_args[pos];
}

//******************************************************************************

