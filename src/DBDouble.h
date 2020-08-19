#ifndef CHAPEAU_DOUBLE_H
#define CHAPEAU_DOUBLE_H

#include <stdio.h>

#include "DBAbstractDataType.h"
#include "Database.h"


namespace chapeau {

class DBDouble : public DBAbstractDataType {

public:
   DBDouble(double value) :
      DBAbstractDataType(std::string("Double")),
      m_value(value) {
   }
   
   DBDouble(const DBDouble& copy) :
      DBAbstractDataType(copy),
      m_value(copy.m_value) {
   }
   
   ~DBDouble() {}
   
   DBDouble& operator=(const DBDouble& copy) {
      if (this == &copy) {
         return *this;
      }
      
      DBAbstractDataType::operator=(copy);
      m_value = copy.m_value;
      
      return *this;
   }
   
   double value() const {
      return m_value;
   }
   
   std::string valueAsString() const override {
      return std::to_string(m_value);
   }

   bool bind(Database* db, int idx, DBStatement* statement) const override {
      return db->bind(this, idx, statement);
   }

   static std::string typeNameAsString() {
      return std::string("Double");
   }
   
private:
   double m_value;

};

}

#endif

