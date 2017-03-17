#ifndef CHAPEAU_INT_H
#define CHAPEAU_INT_H

#include <stdio.h>

#include "DBAbstractDataType.h"
#include "Database.h"


class DBInt : public DBAbstractDataType {
   
public:
   DBInt(int value) :
      DBAbstractDataType(std::string("Int")),
      m_value(value) {
   }
   
   DBInt(const DBInt& copy) :
      DBAbstractDataType(copy),
      m_value(copy.m_value) {
   }
   
   ~DBInt() {}
   
   DBInt& operator=(const DBInt& copy) {
      if (this == &copy) {
         return *this;
      }
      
      DBAbstractDataType::operator=(copy);
      m_value = copy.m_value;
      
      return *this;
   }
   
   int value() const {
      return m_value;
   }
   
   std::string valueAsString() const {
      char buffer[20];
      snprintf(buffer, 20, "%d", m_value);
      return std::string(buffer);
   }

   bool bind(Database* db, int idx, DBStatement* statement) const {
      return db->bind(this, idx, statement);
   }

   static std::string typeNameAsString() {
      return std::string("Int");
   }
   
private:
   int m_value;

};

#endif

