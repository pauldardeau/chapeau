#include <stdlib.h>
#include <time.h>
#include "DBDate.h"

using namespace chapeau;

static const std::string DB_DATE_TYPE_NAME = "Date";

//******************************************************************************

DBDate::DBDate() :
   DBAbstractDataType(DB_DATE_TYPE_NAME) {
}

//******************************************************************************

DBDate::DBDate(double timeIntervalSince1970) :
   DBAbstractDataType(DB_DATE_TYPE_NAME),
   m_dateTime(timeIntervalSince1970) {
}

//******************************************************************************

DBDate::DBDate(const chaudiere::DateTime& dateTime) :
   DBAbstractDataType(DB_DATE_TYPE_NAME),
   m_dateTime(dateTime) {
}

//******************************************************************************

DBDate::DBDate(const std::string& dateAsString) :
   DBAbstractDataType(DB_DATE_TYPE_NAME),
   m_dateTime(dateAsString) {
}

//******************************************************************************
   
DBDate::DBDate(const DBDate& copy) :
   DBAbstractDataType(copy),
   m_dateTime(copy.m_dateTime) {
}

//******************************************************************************
   
DBDate::~DBDate() {
}

//******************************************************************************

DBDate& DBDate::operator=(const DBDate& copy) {
   if (this == &copy) {
      return *this;
   }
      
   DBAbstractDataType::operator=(copy);
   
   m_dateTime = copy.m_dateTime;
      
   return *this;
}

//******************************************************************************

const chaudiere::DateTime& DBDate::getDateTime() const {
   return m_dateTime;
}

//******************************************************************************

double DBDate::timeIntervalSince1970() const {
   return m_dateTime.timeIntervalSince1970();
}

//******************************************************************************

std::string DBDate::stringValue() const {
   return m_dateTime.formattedString();
}

//******************************************************************************
   
std::string DBDate::valueAsString() const {
   return stringValue();
}

//******************************************************************************

bool DBDate::bind(Database* db, int idx, DBStatement* statement) const {
   return db->bind(this, idx, statement);
}

//******************************************************************************

std::string DBDate::typeNameAsString() {
   return DB_DATE_TYPE_NAME;
}

//******************************************************************************

