#ifndef CHAPEAU_LONG_H
#define CHAPEAU_LONG_H

#include <stdio.h>

#include "DBAbstractDataType.h"
#include "Database.h"


namespace chapeau {

class DBLong : public DBAbstractDataType {
   
public:
   DBLong(long value) :
      DBAbstractDataType(std::string("Long")),
      m_value(value) {
   }
   
   DBLong(const DBLong& copy) :
      DBAbstractDataType(copy),
      m_value(copy.m_value) {
   }
   
   ~DBLong() {}
   
   DBLong& operator=(const DBLong& copy) {
      if (this == &copy) {
         return *this;
      }
      
      DBAbstractDataType::operator=(copy);
      m_value = copy.m_value;
      
      return *this;
   }
   
   long value() const {
      return m_value;
   }
   
   std::string valueAsString() const override {
      return std::to_string(m_value);
   }
   
   bool bind(Database* db, int idx, DBStatement* statement) const override {
      return db->bind(this, idx, statement);
   }

   static std::string typeNameAsString() {
      return std::string("Long");
   }
   
private:
   long m_value;

};

}

#endif

