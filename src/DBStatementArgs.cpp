#include "DBStatementArgs.h"
#include "DBAbstractDataType.h"

using namespace chapeau;

//******************************************************************************

DBStatementArgs::DBStatementArgs() {
}

//******************************************************************************

DBStatementArgs::~DBStatementArgs() {
   clear();
}

//******************************************************************************

void DBStatementArgs::clear() {
   for (auto& adt: m_args) {
      delete adt;
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

